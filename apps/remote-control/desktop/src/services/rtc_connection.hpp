#include "rtc_blocking_bridge.hpp"
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

// Owned only by the media supervisor. A new connection never restores a grant.
class RtcConnection {
 public:
  RtcConnection(std::string url, std::string token, std::string peer, bool receive_video,
                const std::atomic<bool>& stop);
  ~RtcConnection();
  sdk::Result<void> publish_screen(int width, int height, bool generated);
  sdk::Result<void> poll();
  sdk::Result<void> status() const;

  struct Packet {
    std::vector<uint8_t> bytes;
    sdk::DataReceiveParams source;
  };

  sdk::Result<std::vector<Packet>> take_data();
  std::future<sdk::Result<void>> send(std::vector<uint8_t>);
  std::shared_ptr<const sdk::VideoFrame> take_frame();
  bool connected() const;
  sdk::Result<void> close();

 private:
  bool owned(const std::string& sid) const;
  sdk::Result<void> subscribe(const rtc::TrackDescriptor&);
  std::unique_ptr<livekit::sdk::app_support::BlockingRtc> engine_;
  sdk::app_support::RtcEventMailbox engine_events_;
  std::unique_ptr<sdk::app_support::DataEventMailbox> data_events_;
  sdk::Failure failure_;
  std::string peer_, peer_sid_;
  const std::atomic<bool>& stop_;
  bool receive_video_, closed_ = false;
  std::map<rtc::TrackId, rtc::TrackDescriptor> discovered_;
  std::set<rtc::TrackId> subscriptions_;
  std::mutex frames_mutex_;
  std::map<rtc::TrackId, std::shared_ptr<const sdk::VideoFrame>> frames_;
};
}  // namespace remote_app
