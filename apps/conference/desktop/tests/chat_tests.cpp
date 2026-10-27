#include "chat.hpp"
#include <iostream>
#include <stdexcept>
using namespace demo;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main() {
  try {
    for (const auto& invalid : std::vector<std::string>{"", " \n\t　", std::string(2001, 'a'), std::string("\xc0\xaf", 2), std::string("x\0y", 3)}) {
      bool rejected = false; try { validate_chat_text(invalid); } catch (...) { rejected = true; } check(rejected, "Invalid text accepted");
    }
    std::string chinese; for (int i = 0; i < 2000; ++i) chinese += "中";
    validate_chat_text(chinese); validate_chat_text("你好\n第二行 😀");
    ChatLog log; livekit::sdk::DataReceiveParams source{"alice", chat_topic, true};
    auto bytes = encode_chat("one", "你好\n第二行 😀");
    check(log.receive(bytes, source, "爱丽丝", "me"), "Chinese receive");
    check(log.messages().back().sender == "alice" && log.messages().back().name == "爱丽丝", "Sender identity mapping");
    check(!log.receive(bytes, source, "spoof", "me") && log.messages().size() == 1, "Duplicate receive");
    check(!log.receive(bytes, source, "Alice", "alice"), "Local echo");
    auto wrong = source; wrong.topic = "other"; check(!log.receive(bytes, wrong, "Alice", "me"), "Wrong topic");
    wrong = source; wrong.reliable = false; check(!log.receive(bytes, wrong, "Alice", "me"), "Unreliable chat");
    wrong = source; wrong.sender_identity = ""; check(!log.receive(bytes, wrong, "Alice", "me"), "Anonymous chat");
    wrong = source; wrong.sender_identity = "bob"; check(log.receive(bytes, wrong, "Bob", "me"), "ID dedup crossed participants");
    check(!log.receive({'{', '}'}, source, "Alice", "me"), "Missing envelope fields");
    check(!log.receive(std::vector<uint8_t>(16385, 'x'), source, "Alice", "me"), "Oversized envelope");
    const auto spoof = livekit::sdk::Json{{"v", 1}, {"id", "two"}, {"text", "<b>literal</b>"}, {"sender", "admin"}}.dump();
    check(log.receive({spoof.begin(), spoof.end()}, source, "Alice", "me") && log.messages().back().sender == "alice", "Payload spoofed sender");
    log.append({"own", "me", "我", "消息", ChatStatus::Pending}); log.interrupt(); log.complete("own", true);
    check(log.messages().back().status == ChatStatus::Unconfirmed, "Late result changed interrupted message");
    for (int i = 0; i < 220; ++i) log.append({std::to_string(i), "alice", "Alice", "消息"});
    check(log.messages().size() == 200 && log.messages().front().id == "20", "History retention");
    std::cout << "PASS chat UTF-8, boundaries, identity, topic, dedup, interruption and retention\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
