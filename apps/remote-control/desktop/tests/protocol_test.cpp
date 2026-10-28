#include "backends/platform/macos/control_input.hpp"
#include "features/remote_control/video_widget.hpp"
#include "infrastructure/emergency_hotkey.hpp"
#include "services/controller.hpp"
#include <iostream>
#include <stdexcept>
using namespace remote_app;
namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class F> void rejected(F &&f, const char *m) {
  bool caught = false;
  try {
    f();
  } catch (...) {
    caught = true;
  }
  check(caught, m);
}
struct MacRecorder : sdk::detail::MacInputApi {
  bool allowed = true, fail_up = false;
  std::vector<sdk::detail::MacInputEvent> events;
  bool available() noexcept override { return allowed; }
  sdk::detail::MacDisplayGeometry display() noexcept override {
    return {2880, 1800, 0, 0, 1440, 900};
  }
  bool send(const sdk::detail::MacInputEvent &e) noexcept override {
    events.push_back(e);
    return !(fail_up && e.type == "text" && !e.down);
  }
};
} // namespace
int main() {
  try {
    unsigned calls = 0;
    auto hotkey =
        register_emergency_hotkey([&](unsigned key, unsigned long &error) {
          ++calls;
          error = 1409;
          return key == 0x78;
        });
    check(calls == 2 && hotkey.available() && hotkey.error == 0,
          "emergency hotkey fallback");
    hotkey = register_emergency_hotkey([](unsigned, unsigned long &error) {
      error = 1409;
      return false;
    });
    check(!hotkey.available() && hotkey.error == 1409,
          "hotkey unavailable is recoverable");
    ControlProtocol p;
    p.grant = {{"grant_id", "g"}, {"epoch", 1}, {"host_identity", "host"}};
    sdk::DataReceiveParams sender;
    sender.sender_identity = "host";
    sender.topic = sdk::remote_control_topic;
    sender.reliable = true;
    sdk::Json state = {
        {"v", 1},        {"type", "layout"},    {"grant_id", "g"},
        {"epoch", 1},    {"layout_version", 1}, {"width", 1280},
        {"height", 720}, {"active", true},      {"text_input", true}};
    auto receive = [&](sdk::Json s, sdk::DataReceiveParams source) {
      auto bytes = s.dump();
      return p.receive({bytes.begin(), bytes.end()}, source);
    };
    auto stranger = sender;
    stranger.sender_identity = "intruder";
    check(!receive(state, stranger), "ignore unauthenticated sender");
    check(receive(state, sender), "accept host layout");
    check(p.next().at("type") == "heartbeat", "handshake before ack");
    check(p.next().at("type") == "release", "release on layout");
    check(p.next().at("type") == "layout_ack", "ack after heartbeat");
    p.push({{"type", "move"}, {"x", 0.1}});
    p.push({{"type", "move"}, {"x", 0.9}});
    p.flush_move();
    check(p.queue.size() == 1 && p.next().at("x") == 0.9,
          "coalesce mouse moves");
    auto now = sdk::ControlClock::now();
    p.text("你好 😀\r\n第二行", now);
    check(p.pending == 1, "pending text");
    rejected([&] { p.text("重复", now); }, "duplicate submission rejected");
    check(p.next().at("type") == "release", "text releases modifiers");
    auto text = p.next();
    check(text.at("text") == "你好 😀\n第二行", "normalize newline");
    state["text_result"] = {{"id", 9}, {"accepted", true}};
    receive(state, sender);
    check(p.pending == 1, "mismatched ack ignored");
    state["text_result"]["id"] = 1;
    receive(state, sender);
    check(p.pending == 0 && p.accepted == 1, "matched ack");
    rejected([&] { p.text(std::string(257, 'x'), now); }, "text limit");
    rejected([&] { p.text("\x01", now); }, "control char");
    rejected([&] { p.text("\xc0\xaf", now); }, "invalid utf8");
    p.text("uncertain", now);
    rejected([&] { p.tick(now + std::chrono::seconds(5)); }, "timeout stops");
    p.finish();
    check(p.queue.size() == 1 && p.next().at("type") == "stop",
          "stop discards pending text");
    state["layout_version"] = 2;
    receive(state, sender);
    state["layout_version"] = 1;
    check(!receive(state, sender), "old layout ignored");
    state["layout_version"] = 2;
    state["active"] = false;
    rejected([&] { receive(state, sender); }, "host revocation");
    p.queue.clear();
    for (int i = 0; i < 256; ++i)
      p.push({{"type", "key"}});
    rejected([&] { p.push({{"type", "key"}}); }, "bounded queue");
    auto r = std::make_shared<MacRecorder>();
    auto input = sdk::detail::mac_input_with_api(r);
    sdk::ControlInput e;
    e.type = "key";
    e.code = "ControlRight";
    e.down = true;
    check(input->apply(e), "right control");
    check(r->events.back().key == 62 && (r->events.back().flags & (1 << 18)),
          "right control code and flags");
    e.code = "KeyA";
    check(input->apply(e) && r->events.back().key == 0,
          "A is valid mac key zero");
    e.code = "ControlRight";
    e.down = false;
    check(input->apply(e) && !(r->events.back().flags & (1 << 18)),
          "modifier released");
    e = {};
    e.type = "button";
    e.positioned = true;
    e.x = 1440;
    e.y = 900;
    e.button = 0;
    e.down = true;
    check(input->apply(e), "mouse down");
    check(r->events.back().x == 720 && r->events.back().y == 450,
          "retina mapping");
    e.type = "move";
    check(input->apply(e) && r->events.back().type == "drag", "drag event");
    e.type = "button";
    e.down = false;
    e.positioned = false;
    check(input->apply(e) && r->events.back().type == "button_release",
          "release keeps cursor position");
    e = {};
    e.type = "text";
    e.text = "😀\n";
    check(input->apply(e), "unicode text");
    check(r->events[r->events.size() - 4].text.size() == 2,
          "surrogate pair preserved");
    r->fail_up = true;
    e.text = "字";
    check(!input->apply(e), "partial text failure");
    r->fail_up = false;
    e.type = "text_release";
    check(input->apply(e), "release retry");
    r->allowed = false;
    e.type = "key";
    e.code = "KeyA";
    check(!input->apply(e), "permission denial");
    auto point = video_point({0, 0, 800, 600}, {1600, 900}, {400, 300});
    check(point && point->x() == 0.5 && point->y() == 0.5,
          "center normalization");
    check(!video_point({0, 0, 800, 600}, {1600, 900}, {400, 20}),
          "black bar rejected");
    QKeyEvent k(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    check(key_code(k) == "KeyA", "key mapping");
    sdk::VideoFrame frame;
    frame.width = 2;
    frame.height = 2;
    frame.stride_y = 2;
    frame.stride_u = frame.stride_v = 1;
    frame.y = {16, 16, 16, 16};
    frame.u = {128};
    frame.v = {128};
    check(convert_frame(frame).pixelColor(0, 0) == QColor(Qt::black),
          "I420 conversion");
    frame.y.clear();
    rejected([&] { convert_frame(frame); }, "truncated frame");
    std::cout
        << "Protocol, input boundary, text, geometry and video tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
