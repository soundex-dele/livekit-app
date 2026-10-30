#include "rtc_result.hpp"
using livekit::sdk::app_support::require_result;
#pragma once
#include "rtc_event_mailbox.hpp"
#include "data_event_mailbox.hpp"
#include <livekit/rtc/engine.hpp>
#include <livekit/remote_control.hpp>
#include <atomic>
#include <set>
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
  T await(sdk::Result<T>& result) {
    return require_result(std::move(result));
  }

  bool owned(const std::string& sid) const;
  void subscribe(const rtc::TrackDescriptor&);
  std::unique_ptr<rtc::engine::RtcEngine> engine_;
  sdk::app_support::RtcEventMailbox engine_events_;
  sdk::app_support::DataEventMailbox data_events_;
  std::string peer_, peer_sid_;
  const std::atomic<bool>& stop_;
  bool receive_video_, closed_ = false;
  std::map<rtc::TrackId, rtc::TrackDescriptor> discovered_;
  std::set<rtc::TrackId> subscriptions_;
  std::mutex frames_mutex_;
  std::map<rtc::TrackId, std::shared_ptr<const sdk::VideoFrame>> frames_;
};
}  // namespace remote_app
