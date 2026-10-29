#pragma once
#include "services/session.hpp"
#include <QImage>
#include <optional>
namespace remote_app {
struct ControllerSnapshot {
  std::string state = "idle", error, text_status;
  bool finished = true, text_supported = false, text_pending = false;
  uint64_t layout = 0, text_accepted = 0, frames = 0;
  int width = 0, height = 0;
  QImage image;
};
// Pure protocol state; caller serializes access. No native input or networking.
class ControlProtocol {
public:
  sdk::Json grant, layout;
  std::deque<sdk::Json> queue;
  std::optional<sdk::Json> move;
  uint64_t seq = 0, text_id = 0, pending = 0, accepted = 0;
  std::string text_status;
  sdk::ControlClock::time_point text_deadline{};
  void push(sdk::Json event);
  void flush_move();
  bool receive(const std::vector<uint8_t> &, const sdk::DataReceiveParams &);
  sdk::Json next();
  void text(std::string value,
            sdk::ControlClock::time_point now = sdk::ControlClock::now());
  void tick(sdk::ControlClock::time_point now = sdk::ControlClock::now());
  void finish();
};
QImage convert_frame(const sdk::VideoFrame &);
class Controller {
public:
  ~Controller();
  void start(std::string url, std::string code, std::string name);
  void stop();
  void input(sdk::Json event);
  void release();
  void send_text(std::string text);
  ControllerSnapshot snapshot() const;

private:
  void run(std::string url, std::string code, std::string name);
  void media(std::string url, sdk::Json grant, std::string credential);
  void fail(std::string message);
  mutable std::mutex mutex_;
  ControllerSnapshot snapshot_;
  ControlProtocol protocol_;
  std::thread worker_, rtc_;
  std::atomic<bool> stop_{false};
};
} // namespace remote_app
