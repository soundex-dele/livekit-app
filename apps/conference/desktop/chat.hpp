#pragma once
#include <livekit/json.hpp>
#include <string>
#include <vector>
#include <deque>
#include <cstdint>
#include <string_view>

namespace demo {
struct ChatSource { std::string sender_identity, topic; bool reliable = true; };
inline constexpr char chat_topic[] = "av.conference.chat.v1";
enum class ChatStatus { Received, Pending, Sent, Unconfirmed };
struct ChatMessage {
  std::string id, sender, name, text;
  ChatStatus status = ChatStatus::Received;
};
// Throw a user-facing reason for invalid UTF-8, empty messages or >2000 scalars.
void validate_chat_text(std::string_view text);
std::vector<uint8_t> encode_chat(const std::string& id, const std::string& text);
class ChatLog {
 public:
  void append(ChatMessage message);
  bool receive(const std::vector<uint8_t>& payload, const demo::ChatSource& params,
               const std::string& name, const std::string& local_identity);
  void complete(const std::string& id, bool confirmed);
  void interrupt();
  const std::deque<ChatMessage>& messages() const { return messages_; }
  uint64_t revision() const { return revision_; }
 private:
  std::deque<ChatMessage> messages_;
  std::deque<std::pair<std::string, std::string>> seen_;
  uint64_t revision_ = 0;
};
}
