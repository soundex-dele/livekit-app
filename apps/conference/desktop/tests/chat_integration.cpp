#include "session.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
using namespace std::chrono_literals;
namespace sdk = livekit::sdk;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
template<class F> void until(F condition, const char* why) {
  const auto end = std::chrono::steady_clock::now() + 25s;
  while (!condition()) { check(std::chrono::steady_clock::now() < end, why); std::this_thread::sleep_for(20ms); }
}
int main() {
  try {
    const auto env = [](const char* key) { const char* value = std::getenv(key); if (!value) throw std::runtime_error("Missing environment"); return std::string(value); };
    sdk::LiveKitAPI api({env("LIVEKIT_URL"), {env("LIVEKIT_API_KEY"), env("LIVEKIT_API_SECRET"), ""}, false});
    const std::string name = "conference-chat-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    struct Cleanup { sdk::LiveKitAPI& api; std::string room; ~Cleanup() { try { api.room().delete_room(room); api.room().delete_room(room + "-other"); } catch (...) {} } } cleanup{api, name};
    auto options = [&](const std::string& who, const std::string& room) {
      sdk::RoomOptions o; o.url = env("LIVEKIT_URL"); o.room = room; o.audio_playout = false;
      o.token = api.room().create_token().set_identity(who).set_name(who)
        .set_video_grant({{"roomJoin", true}, {"room", room}, {"canPublish", false}, {"canPublishData", true}, {"canSubscribe", true}}).to_jwt();
      return o;
    };
    std::cerr << "chat: create sessions\n";
    demo::Session alice, bob, other;
    check(!alice.send_chat("离线消息"), "Offline chat accepted");
    std::cerr << "chat: join sessions\n";
    alice.start(options("Alice", name)); bob.start(options("Bob", name)); other.start(options("Other", name + "-other"));
    until([&] { return alice.snapshot().state == demo::State::Connected && bob.snapshot().state == demo::State::Connected &&
      alice.snapshot().participants.size() == 2 && bob.snapshot().participants.size() == 2 && other.snapshot().state == demo::State::Connected; }, "Chat participants did not connect");
    std::cerr << "chat: participants connected\n";
    check(!alice.send_chat(" \n ") && !alice.snapshot().chat_error.empty(), "Empty message accepted");
    const std::string hello = "你好，会议聊天！\n第二行 😀";
    check(alice.send_chat(hello), "Chat enqueue failed");
    std::cerr << "chat: first message queued\n";
    until([&] { const auto s = bob.snapshot(); return !s.chat.messages().empty() && s.chat.messages().back().text == hello; }, "Chinese chat not received");
    until([&] { return !alice.snapshot().sending_chat; }, "Send never completed");
    check(alice.snapshot().chat.messages().size() == 1 && alice.snapshot().chat.messages().back().status == demo::ChatStatus::Sent, "Local send status/echo");
    check(bob.snapshot().chat.messages().back().sender == "Alice", "Received identity mismatch");
    check(bob.send_chat("已收到，中文回复。"), "Reply enqueue");
    until([&] { const auto s = alice.snapshot(); return s.chat.messages().size() == 2 && s.chat.messages().back().sender == "Bob"; }, "Reply missing");
    check(other.snapshot().chat.messages().empty(), "Chat leaked across rooms");
    bob.stop(); until([&] { return bob.snapshot().finished; }, "Leave timed out");
    check(bob.snapshot().chat.messages().empty() && !bob.send_chat("离会后"), "Chat survived leave");
    bob.start(options("Bob", name)); until([&] { return bob.snapshot().state == demo::State::Connected; }, "Rejoin timed out");
    check(bob.snapshot().chat.messages().empty(), "Rejoin restored stale history");
    check(bob.send_chat("重新入会后的消息"), "Send after rejoin");
    until([&] { const auto s = alice.snapshot(); return !s.chat.messages().empty() && s.chat.messages().back().text == "重新入会后的消息"; }, "Rejoined chat missing");
    check(other.snapshot().chat.messages().empty(), "Later chat leaked across rooms");
    alice.stop(); bob.stop(); other.stop();
    until([&] { return alice.snapshot().finished && bob.snapshot().finished && other.snapshot().finished; }, "Cleanup failed");
    std::cout << sdk::Json{{"passed", true}, {"unicode_multiline", true}, {"bidirectional", true}, {"room_isolation", true}, {"leave_rejoin", true}, {"native_devices", false}}.dump() << '\n';
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
