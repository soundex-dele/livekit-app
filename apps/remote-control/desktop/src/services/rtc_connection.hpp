#pragma once
#include <livekit/media_engine.hpp>
#include <livekit/remote_control.hpp>
#include <atomic>
#include <mutex>
#include <map>

namespace remote_app {
namespace sdk = livekit::sdk;
namespace rtc = sdk::client;

// Owned by the media supervisor. Reconnection never restores a control grant.
class RtcConnection : private rtc::RoomObserver {
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
  // Success denotes local submission; protocol acknowledgement/watchdog confirms delivery.
  sdk::Result<void> send(std::vector<uint8_t>);
  std::shared_ptr<const sdk::VideoFrame> take_frame();
  bool connected() const;
  void close();

 private:
  void on_room_event(const rtc::RoomEvent&) override;
  bool owned(const std::string& sid) const;
  std::unique_ptr<rtc::MediaEngine> engine_;
  sdk::Failure failure_;
  std::string peer_, peer_sid_;
  const std::atomic<bool>& stop_;
  bool receive_video_, closed_ = false;
  std::shared_ptr<rtc::MediaTrack> screen_;
  std::map<rtc::TrackSid, std::shared_ptr<rtc::MediaTrack>> subscriptions_;
  mutable std::mutex mutex_;
  sdk::Failure event_failure_;
  std::vector<Packet> packets_;
  std::map<rtc::TrackSid, std::shared_ptr<const sdk::VideoFrame>> frames_;
};
}  // namespace remote_app
