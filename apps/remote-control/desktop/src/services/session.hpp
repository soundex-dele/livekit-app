#pragma once
#include "domain/snapshot.hpp"
#include "infrastructure/http.hpp"
#include <livekit/media.hpp>
#include <livekit/remote_control.hpp>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

namespace remote_app {
namespace sdk = livekit::sdk;

class Session {
 public:
  Session(bool fixture = false, std::shared_ptr<sdk::ControlInputSink> sink = {});
  ~Session();
  void start(std::string url, std::string account = {}, std::string password = {},
             std::string organization = {});
  void approve();
  void reject();
  void stop();
  Snapshot snapshot() const;

 private:
  void run(std::string url, std::string account, std::string password);
  void media(std::string url, std::string id, std::string credential);
  void update(const sdk::Json&, sdk::ControlClock::time_point started);
  mutable std::mutex mutex_;
  Snapshot snapshot_;
  sdk::ControlGrant grant_;
  sdk::Failure failure_;
  std::shared_ptr<sdk::RemoteControlSession> control_;
  std::thread worker_, rtc_;
  std::atomic<bool> stop_{false};
  std::atomic<int> action_{0};
  bool fixture_;
  std::string account_token_, organization_;
};
}  // namespace remote_app
