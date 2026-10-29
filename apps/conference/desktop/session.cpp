#include "session.hpp"
#include <set>
#include <chrono>
#include <algorithm>

namespace demo {
const char* state_name(State state) {
  switch (state) {
    case State::Idle: return "未入会"; case State::Connecting: return "正在连接";
    case State::Reconnecting: return "正在重连";
    case State::Connected: return "已连接"; case State::Disconnecting: return "正在离会";
    default: return "连接失败";
  }
}
void Snapshot::update(Participant local, const std::vector<Participant>& remote) {
  std::set<std::string> current;
  participants.clear(); local_sid = local.sid;
  for (const auto& track : local.tracks)
    if (!track.local_id.empty()) current.insert(track.local_id);
  if (!local.sid.empty()) participants[local.sid] = std::move(local);
  for (const auto& p : remote) {
    if (p.state == 3) continue;
    participants[p.sid] = p;
    for (const auto& t : p.tracks) if (t.subscribed) current.insert(t.sid);
  }
  // Unknown/removed tracks cannot accumulate pixels indefinitely. A frame that
  // precedes metadata is replaced by the next decoded frame once subscribed.
  std::erase_if(frames, [&](const auto& item) { return !current.contains(item.first); });
}
std::vector<Tile> tiles(const Snapshot& s) {
  std::vector<Tile> result;
  for (const auto& [sid, p] : s.participants) {
    if (sid == s.local_sid) {
      for (const auto& track : p.tracks) {
        const auto id = track.local_id;
        if (auto frame = s.frames.find(id); frame != s.frames.end())
          result.push_back({id, "本地预览 / " + track.name, track.muted ? "视频已关闭" : "",
            track.muted ? nullptr : frame->second});
      }
      continue;
    }
    const auto label = p.name.empty() ? p.identity : p.name;
    bool video = false;
    for (const auto& t : p.tracks) {
      if (t.kind != rtc::PublicationKind::video) continue;
      video = true;
      Tile tile{t.sid, label + " / " + (t.name.empty() ? t.sid : t.name), t.muted ? "视频已关闭" : "等待画面", {}};
      if (!t.muted) if (auto f = s.frames.find(t.sid); f != s.frames.end()) { tile.frame = f->second; tile.status.clear(); }
      result.push_back(std::move(tile));
    }
    if (!video) result.push_back({sid, label, "未开启视频", {}});
  }
  return result;
}
bool Session::enable_microphone() {
 std::lock_guard lock(mutex_);if(data_.state!=State::Connected||data_.changing_video)return false;
 VideoCommand c;c.microphone=true;video_command_=c;data_.changing_video=true;data_.video_error.clear();return true;
}
bool Session::add_video() {
  sdk::MediaSpec spec; spec.demo = true; return add_video(spec);
}
bool Session::add_video(sdk::MediaSpec spec, rtc::PublicationSource source) {
  if (!spec.video) return false;
  std::lock_guard lock(mutex_);
  if (data_.state != State::Connected || data_.changing_video) return false;
  const auto local = data_.participants.find(data_.local_sid);
  if (local == data_.participants.end()) return false;
  const auto& tracks = local->second.tracks;
  if (std::count_if(tracks.begin(), tracks.end(), [](const auto& t) { return t.kind == rtc::PublicationKind::video; }) >= max_test_videos) return false;
  video_command_ = VideoCommand{"", std::move(spec), {}, false, source}; data_.changing_video = true; data_.video_error.clear();
  return true;
}
bool Session::remove_video(const std::string& sid) {
  std::lock_guard lock(mutex_);
  if (data_.state != State::Connected || data_.changing_video) return false;
  const auto local = data_.participants.find(data_.local_sid);
  if (local == data_.participants.end()) return false;
  const auto& tracks = local->second.tracks;
  if (std::none_of(tracks.begin(), tracks.end(), [&](const auto& t) { return t.sid == sid && t.kind == rtc::PublicationKind::video; })) return false;
  video_command_ = VideoCommand{sid}; data_.changing_video = true; data_.video_error.clear();
  return true;
}
Snapshot Session::snapshot() const { std::lock_guard lock(mutex_); return data_; }
bool Session::send_chat(const std::string& text) {
  std::lock_guard lock(mutex_);
  if (data_.state != State::Connected || data_.sending_chat || chat_command_ || chat_in_flight_) {
    data_.chat_error = "请等待会议连接就绪或上一条消息发送完成。"; return false;
  }
  try { validate_chat_text(text); }
  catch (const std::exception& e) { data_.chat_error = e.what(); return false; }
  const auto local = data_.participants.find(data_.local_sid);
  if (local == data_.participants.end()) { data_.chat_error = "正在获取参会信息，请稍后再试。"; return false; }
  ChatMessage message{chat_epoch_ + "-" + std::to_string(++chat_sequence_), local->second.identity, "我", text, ChatStatus::Pending};
  data_.chat.append(message); chat_command_ = std::move(message); data_.sending_chat = true; data_.chat_error.clear();
  return true;
}
bool Session::set_muted(const std::string& sid, bool muted) {
  std::lock_guard lock(mutex_);
  if (data_.state != State::Connected || data_.changing_video) return false;
  const auto local = data_.participants.find(data_.local_sid);
  if (local == data_.participants.end()) return false;
  if (std::none_of(local->second.tracks.begin(), local->second.tracks.end(), [&](const auto& t) { return t.sid == sid; })) return false;
  video_command_ = VideoCommand{sid, {}, muted}; data_.changing_video = true; data_.video_error.clear(); return true;
}
}
