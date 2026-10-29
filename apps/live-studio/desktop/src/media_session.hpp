#pragma once
#include <livekit/rtc/engine.hpp>
#include <livekit/studio.hpp>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace studio_app {
// The supervisor owns the room and all graph lifecycle calls. Business/UI
// threads only read snapshots or request stop; codec callbacks use mailboxes.
class MediaSession {
 public:
  using Statistics = std::function<void(const livekit::sdk::rtc::TransportStatistics&)>;
  MediaSession(livekit::sdk::rtc::RoomOptions, std::shared_ptr<livekit::sdk::Studio>,
               bool interaction, const std::atomic<bool>& closing, Statistics = {});
  ~MediaSession();

  bool connected() const {
    return connected_;
  }

  bool finished() const {
    return finished_;
  }

  std::string error() const;

  uint64_t received_video() const {
    return received_video_;
  }

  uint64_t received_audio() const {
    return received_audio_;
  }

  size_t subscriptions() const {
    return subscriptions_;
  }

 private:
  void run(livekit::sdk::rtc::RoomOptions, std::shared_ptr<livekit::sdk::Studio>, bool,
           const std::atomic<bool>&, Statistics, std::promise<void>);
  std::atomic<bool> stop_{false}, connected_{false}, finished_{false};
  std::atomic<uint64_t> received_video_{0}, received_audio_{0};
  std::atomic<size_t> subscriptions_{0};
  mutable std::mutex mutex_;
  std::string error_;
  std::jthread worker_;
};
}  // namespace studio_app
