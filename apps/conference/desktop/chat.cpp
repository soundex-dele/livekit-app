#include "chat.hpp"
#include <algorithm>
#include <stdexcept>

namespace demo {
void validate_chat_text(std::string_view text) {
  if (text.size() > 8000) throw std::invalid_argument("每条消息最多 2000 个字符。");
  size_t count = 0; bool visible = false;
  for (size_t i = 0; i < text.size();) {
    const auto first = static_cast<unsigned char>(text[i++]);
    uint32_t cp = first; int n = 0;
    if (first >= 0x80) {
      n = first >= 0xc2 && first <= 0xdf ? 1 : first >= 0xe0 && first <= 0xef ? 2 : first >= 0xf0 && first <= 0xf4 ? 3 : -1;
      if (n < 0 || i + n > text.size()) throw std::invalid_argument("文字编码无效，请重新输入。");
      cp = first & ((1u << (6 - n)) - 1);
      for (int j = 0; j < n; ++j) {
        auto b = static_cast<unsigned char>(text[i++]);
        if ((b & 0xc0) != 0x80) throw std::invalid_argument("文字编码无效，请重新输入。");
        cp = (cp << 6) | (b & 63);
      }
      if (cp < (n == 1 ? 0x80u : n == 2 ? 0x800u : 0x10000u) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
        throw std::invalid_argument("文字编码无效，请重新输入。");
    }
    if ((cp < 32 && cp != '\n' && cp != '\t' && cp != '\r') || cp == 127)
      throw std::invalid_argument("消息包含不可显示的控制字符。");
    if (++count > 2000) throw std::invalid_argument("每条消息最多 2000 个字符。");
    visible |= cp != ' ' && cp != '\t' && cp != '\r' && cp != '\n' && cp != 0x3000 && cp != 0xa0;
  }
  if (!visible) throw std::invalid_argument("请输入消息内容。");
}
std::vector<uint8_t> encode_chat(const std::string& id, const std::string& text) {
  validate_chat_text(text);
  const auto json = livekit::sdk::Json{{"v", 1}, {"id", id}, {"text", text}}.dump();
  return {json.begin(), json.end()};
}
void ChatLog::append(ChatMessage message) {
  messages_.push_back(std::move(message));
  if (messages_.size() > 200) messages_.pop_front();
  ++revision_;
}
bool ChatLog::receive(const std::vector<uint8_t>& payload, const livekit::sdk::DataReceiveParams& params,
                      const std::string& name, const std::string& local_identity) {
  if (params.topic != chat_topic || !params.reliable || params.sender_identity.empty() || params.sender_identity == local_identity || payload.size() > 16384) return false;
  try {
    const auto json = livekit::sdk::Json::parse(payload.begin(), payload.end(),
      [](int depth, livekit::sdk::Json::parse_event_t, livekit::sdk::Json&) {
        if (depth > 8) throw std::invalid_argument("Chat envelope nesting limit");
        return true;
      });
    if (!json.is_object() || json.at("v") != 1) return false;
    auto id = json.at("id").get<std::string>(), text = json.at("text").get<std::string>();
    if (id.empty() || id.size() > 64 || !std::all_of(id.begin(), id.end(), [](unsigned char c) {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'; })) return false;
    validate_chat_text(text);
    const auto key = std::pair{params.sender_identity, id};
    if (std::find(seen_.begin(), seen_.end(), key) != seen_.end()) return false;
    seen_.push_back(key); if (seen_.size() > 512) seen_.pop_front();
    append({std::move(id), params.sender_identity, name.empty() ? params.sender_identity : name, std::move(text)});
    return true;
  } catch (const std::exception&) { return false; }
}
void ChatLog::complete(const std::string& id, bool confirmed) {
  for (auto& message : messages_) if (message.id == id && message.status == ChatStatus::Pending) {
    message.status = confirmed ? ChatStatus::Sent : ChatStatus::Unconfirmed; ++revision_; break;
  }
}
void ChatLog::interrupt() {
  for (auto& message : messages_) if (message.status == ChatStatus::Pending) {
    message.status = ChatStatus::Unconfirmed; ++revision_;
  }
}
}
