#pragma once
#include "rtc_event_mailbox.hpp"
#include <livekit/rtc/engine.hpp>
#include <livekit/remote_control.hpp>
#include <atomic>
#include <mutex>
#include <thread>

namespace remote_app {
namespace sdk = livekit::sdk;
namespace rtc = sdk::rtc;

class ConnectionEnded final : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Owned only by the media supervisor. A new connection never restores a grant.
class RtcConnection {
 public:
  RtcConnection(std::string url, std::string token, std::string peer, bool receive_video,
                const std::atomic<bool>& stop);
  ~RtcConnection();
  void publish_screen(int width, int height, bool generated);
  void poll();

  struct Packet {
    std::vector<uint8_t> bytes;
    sdk::DataReceiveParams source;
  };

  std::vector<Packet> take_data();
  std::future<void> send(std::vector<uint8_t>);
  std::shared_ptr<const sdk::VideoFrame> take_frame();
  bool connected() const;
  void close();

 private:
  template <class T>
  T await(std::future<T>& future) {
    using namespace std::chrono_literals;
    while (future.wait_for(0ms) != std::future_status::ready) {
      if (stop_)
        throw std::runtime_error("Remote connection cancelled");

      std::this_thread::sleep_for(5ms);
    }
    if (stop_)
      throw std::runtime_error("Remote connection cancelled");
    return future.get();
  }

  bool owned(const std::string& sid) const;
  void subscribe(const rtc::TrackDescriptor&);
  std::unique_ptr<rtc::engine::RtcEngine> engine_;
  sdk::app_support::RtcEventMailbox engine_events_;
  std::string peer_, peer_sid_;
  const std::atomic<bool>& stop_;
  bool receive_video_, closed_ = false;
  std::map<rtc::TrackId, rtc::TrackDescriptor> discovered_;
  std::map<rtc::TrackId, std::future<rtc::TrackId>> subscriptions_;
  std::mutex frames_mutex_;
  std::map<rtc::TrackId, std::shared_ptr<const sdk::VideoFrame>> frames_;
};
}  // namespace remote_app
