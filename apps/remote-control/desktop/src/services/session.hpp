#pragma once
#include "domain/snapshot.hpp"
#include "infrastructure/http.hpp"
#include <livekit/room.hpp>
#include <livekit/remote_control.hpp>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

namespace remote_app {
namespace sdk=livekit::sdk;
class Session {
 public:
  Session(bool fixture=false,std::shared_ptr<sdk::ControlInputSink> sink={});
  ~Session();
  void start(std::string url);
  void approve();
  void reject();
  void stop();
  Snapshot snapshot()const;
 private:
  void run(std::string url);
  void media(std::string url,std::string id,std::string credential);
  void update(const sdk::Json&,sdk::ControlClock::time_point started);
  mutable std::mutex mutex_;
  Snapshot snapshot_;
  sdk::ControlGrant grant_;
  std::deque<std::pair<std::vector<uint8_t>,sdk::DataReceiveParams>> incoming_;
  std::shared_ptr<sdk::Room> room_;
  std::shared_ptr<sdk::RemoteControlSession> control_;
  std::thread worker_,rtc_;
  std::atomic<bool> stop_{false};
  std::atomic<int> action_{0};
  std::stop_source cancellation_;
  bool fixture_;
};
}
