#include "media_session.hpp"
#include <livekit/auth.hpp>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;
namespace sdk = livekit::sdk;
namespace client = sdk::client;

int main() {
  const auto* url = std::getenv("LIVEKIT_URL");
  const auto* key = std::getenv("LIVEKIT_API_KEY");
  const auto* secret = std::getenv("LIVEKIT_API_SECRET");
  if (!url || !key || !secret) {
    return 77;
  }
  const sdk::Json grant{{"roomJoin", true},
                        {"room", "studio-media-migration"},
                        {"canPublish", true},
                        {"canSubscribe", true},
                        {"canPublishData", true}};
  auto a = sdk::make_token(key, secret, "studio-a", grant);
  auto b = sdk::make_token(key, secret, "studio-b", grant);
  if (!a || !b) {
    return 1;
  }
  sdk::StudioOptions canvas;
  canvas.software_gpu = true;
  auto first = std::make_shared<sdk::Studio>(canvas);
  auto second = std::make_shared<sdk::Studio>(canvas);
  if (!first->status() || !second->status()) {
    std::cerr << first->snapshot().error << " " << second->snapshot().error << std::endl;
    return 2;
  }
  sdk::StudioSource color;
  color.id = "synthetic";
  color.color = {0.7f, 0.2f, 0.1f, 1};
  sdk::StudioScene scene;
  scene.sources.push_back(color);
  if (!first->set_scene(scene) || !second->set_scene(scene)) {
    return 3;
  }
  client::ConnectionOptions options;
  options.url = url;
  options.token = *a;
  options.connect_timeout = 10;
  options.operation_timeout = 5s;
  options.reconnect.enabled = false;
  std::atomic<bool> closing{false};
  std::atomic<unsigned> reports{0};
  auto stats = [&](const client::PeerStatistics& value) {
    if (!value.json.empty()) {
      ++reports;
    }
  };
  studio_app::MediaSession one(options, first, true, closing, stats);
  options.token = *b;
  auto two = std::make_unique<studio_app::MediaSession>(options, second, true, closing, stats);
  const auto deadline = client::SteadyClock::now() + 20s;
  while (client::SteadyClock::now() < deadline) {
    if (!one.error().empty() || !two->error().empty()) {
      std::cerr << one.error() << " " << two->error() << '\n';
      return 4;
    }
    if (one.received_video() >= 15 && two->received_video() >= 15 && one.received_audio() >= 30 &&
        two->received_audio() >= 30 && reports >= 4) {
      break;
    }
    std::this_thread::sleep_for(20ms);
  }
  std::cout << "studio counts video=" << one.received_video() << "," << two->received_video()
            << " audio=" << one.received_audio() << "," << two->received_audio()
            << " subscriptions=" << one.subscriptions() << "," << two->subscriptions()
            << " reports=" << reports.load() << " source_frames=" << first->snapshot().frames
            << "," << second->snapshot().frames << " source_errors=" << first->snapshot().error
            << "," << second->snapshot().error << std::endl;
  if (one.received_video() < 15 || two->received_video() < 15 || one.received_audio() < 30 ||
      two->received_audio() < 30 || reports < 4) {
    return 5;
  }
  two.reset();
  auto until = client::SteadyClock::now() + 5s;
  while (one.subscriptions() && client::SteadyClock::now() < until) {
    std::this_thread::sleep_for(20ms);
  }
  if (one.subscriptions() || !one.error().empty()) {
    return 6;
  }
  std::cout << "PASS studio bidirectional decoded video/audio, statistics and peer cleanup\n";
  return 0;
}
