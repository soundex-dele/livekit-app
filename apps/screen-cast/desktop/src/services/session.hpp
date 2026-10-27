#pragma once
#include "domain/snapshot.hpp"
#include <livekit/room.hpp>
#include <atomic>
#include <mutex>
#include <thread>
#include <functional>
namespace screen_cast {
namespace sdk=livekit::sdk;
class Session {
 public:
 explicit Session(bool fixture=false,std::function<bool()> permission={}):fixture_(fixture),permission_(std::move(permission)){}
 ~Session();
 void start(std::string url,std::string code,sdk::ScreenCaptureDevice display);
 void stop();
 Snapshot snapshot()const;
 private:
 void run(std::string url,std::string code,sdk::ScreenCaptureDevice display);
 mutable std::mutex mutex_;
 Snapshot snapshot_;
 std::shared_ptr<sdk::Room> room_;
 std::thread worker_;
 std::atomic<bool> stop_{false};
 std::stop_source cancellation_;
 bool fixture_;
 std::function<bool()> permission_;
};
}
