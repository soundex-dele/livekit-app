#include "services/rtc_connection.hpp"
#include <livekit/auth.hpp>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;
namespace sdk = livekit::sdk;

int main() {
  const auto* url = std::getenv("LIVEKIT_URL");
  const auto* key = std::getenv("LIVEKIT_API_KEY");
  const auto* secret = std::getenv("LIVEKIT_API_SECRET");
  if (!url || !key || !secret) {
    return 77;
  }
  const sdk::Json grant{{"roomJoin", true},
                        {"room", "remote-media-migration"},
                        {"canPublish", true},
                        {"canSubscribe", true},
                        {"canPublishData", true}};
  auto host_token = sdk::make_token(key, secret, "host", grant);
  auto viewer_token = sdk::make_token(key, secret, "viewer", grant);
  if (!host_token || !viewer_token) {
    return 1;
  }
  std::atomic<bool> stop{false};
  remote_app::RtcConnection host(url, *host_token, "viewer", false, stop);
  remote_app::RtcConnection viewer(url, *viewer_token, "host", true, stop);
  if (!host.status() || !viewer.status() || !host.publish_screen(320, 180, true)) {
    return 2;
  }
  if (!host.send({1, 2, 3}) || !viewer.send({4, 5, 6})) {
    return 3;
  }
  unsigned frames = 0;
  bool host_data = false, viewer_data = false;
  auto deadline = sdk::ControlClock::now() + 15s;
  while (sdk::ControlClock::now() < deadline && (frames < 20 || !host_data || !viewer_data)) {
    if (!host.poll() || !viewer.poll()) {
      std::cerr << "media poll failed\n";
      return 4;
    }
    if (auto frame = viewer.take_frame()) {
      if (frame->width != 320 || frame->height != 180) {
        return 5;
      }
      ++frames;
    }
    auto a = host.take_data(), b = viewer.take_data();
    if (!a || !b) {
      return 6;
    }
    for (const auto& packet : *a) {
      host_data |= packet.bytes == std::vector<uint8_t>{4, 5, 6} &&
                   packet.source.sender_identity == "viewer";
    }
    for (const auto& packet : *b) {
      viewer_data |=
          packet.bytes == std::vector<uint8_t>{1, 2, 3} && packet.source.sender_identity == "host";
    }
    std::this_thread::sleep_for(5ms);
  }
  if (frames < 20 || !host_data || !viewer_data) {
    return 7;
  }
  host.close();
  bool revoked = false;
  deadline = sdk::ControlClock::now() + 5s;
  while (sdk::ControlClock::now() < deadline && !revoked) {
    revoked = !viewer.poll();
    std::this_thread::sleep_for(10ms);
  }
  viewer.close();
  if (!revoked) {
    return 8;
  }
  std::cout << "PASS synthetic screen decoding, directed data, peer departure revocation\n";
  return 0;
}
