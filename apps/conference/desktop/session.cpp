#include "session.hpp"
#include <set>
#include <chrono>
#include <algorithm>
#include <random>

namespace demo {
const char* state_name(State state) {
  switch (state) {
    case State::Idle: return "未入会"; case State::Connecting: return "正在连接";
    case State::Reconnecting: return "正在重连";
    case State::Connected: return "已连接"; case State::Disconnecting: return "正在离会";
    default: return "连接失败";
  }
}
void Snapshot::update(sdk::ParticipantInfo local, const std::vector<sdk::ParticipantInfo>& remote) {
  std::set<std::string> current;
  participants.clear(); local_sid = local.sid;
  for (const auto& track : local.tracks)
    if (!track.client_id.empty()) current.insert("local-" + track.client_id);
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
        const auto id = "local-" + track.client_id;
        if (auto frame = s.frames.find(id); frame != s.frames.end())
          result.push_back({id, "本地预览 / " + track.name, track.muted ? "视频已关闭" : "",
            track.muted ? nullptr : frame->second});
      }
      continue;
    }
    const auto label = p.name.empty() ? p.identity : p.name;
    bool video = false;
    for (const auto& t : p.tracks) {
      if (t.kind != sdk::TrackKind::video) continue;
      video = true;
      Tile tile{t.sid, label + " / " + (t.name.empty() ? t.sid : t.name), t.muted ? "视频已关闭" : "等待画面", {}};
      if (!t.muted) if (auto f = s.frames.find(t.sid); f != s.frames.end()) { tile.frame = f->second; tile.status.clear(); }
      result.push_back(std::move(tile));
    }
    if (!video) result.push_back({sid, label, "未开启视频", {}});
  }
  return result;
}
Session::Session() : room_([this] {
  sdk::RoomCallback callbacks;
  callbacks.on_reconnecting = [this](const sdk::ReconnectInfo&) {
    std::lock_guard lock(mutex_);
    if (data_.state != State::Disconnecting) data_.state = State::Reconnecting;
    video_command_.reset(); data_.changing_video = false;
    chat_command_.reset(); data_.chat.interrupt(); data_.sending_chat = chat_in_flight_;
  };
  callbacks.on_data_received = [this](const std::vector<uint8_t>& payload, const sdk::DataReceiveParams& params) {
    if (params.topic != chat_topic) return;
    const auto sender = room_.participant_by_identity(params.sender_identity);
    if (!sender) return;
    const auto local_identity = room_.local_participant().identity();
    const auto name = sender->info().name;
    std::lock_guard lock(mutex_);
    if (!data_.finished && data_.state != State::Disconnecting) data_.chat.receive(payload, params, name, local_identity);
  };
  callbacks.on_track_unsubscribed = [this](const sdk::TrackPublication& t, const sdk::RemoteParticipant&) {
    std::lock_guard lock(mutex_); data_.frames.erase(t.sid());
  };
  return callbacks;
}()) {}
bool Session::enable_microphone() {
 std::lock_guard lock(mutex_);if(data_.state!=State::Connected||data_.changing_video)return false;
 VideoCommand c;c.microphone=true;video_command_=c;data_.changing_video=true;data_.video_error.clear();return true;
}
Session::~Session() { stop(); if (worker_.joinable()) worker_.join(); }
void Session::start(sdk::RoomOptions options) {
  if (std::count_if(options.media.begin(), options.media.end(), [](const auto& m) { return m.video; }) > max_test_videos)
    throw std::invalid_argument("最多支持同时发布 8 路视频");
  { std::lock_guard lock(mutex_); if (!data_.finished) throw std::logic_error("会议正在进行中"); }
  if (worker_.joinable()) worker_.join();
  { std::lock_guard lock(mutex_); data_ = {}; video_command_.reset(); chat_command_.reset();
    std::random_device random; chat_epoch_ = std::to_string(random()) + "-" + std::to_string(random()); chat_sequence_ = 0; chat_in_flight_ = false;
    data_.state = State::Connecting; data_.finished = false; data_.room = options.room; }
  options.video_playout = false;
  options.video_frame_handler = [this](const sdk::VideoFrame& frame) {
    auto owned = std::make_shared<sdk::VideoFrame>(frame);
    std::lock_guard lock(mutex_);
    if (data_.state != State::Disconnecting) { data_.frames[frame.track_id] = std::move(owned); ++data_.received_frames; }
  };
  try {
    worker_ = std::jthread([this, options = std::move(options)](std::stop_token stop) mutable {
      std::string error;
      auto redact = [&](std::string message) {
        if (!options.token.empty()) for (size_t pos; (pos = message.find(options.token)) != std::string::npos;) message.replace(pos, options.token.size(), "[redacted]");
        return message;
      };
      try {
        // Blocking joins/futures belong on this supervisor, never the UI or SDK callback thread.
        auto media = std::move(options.media); options.media.clear();
        room_.join_with_token(options.url, options.token, options, stop);
        int video_number = 0;
        std::future<void> chat_send;
        std::string sending_id;
        auto publish = [&](const sdk::MediaSpec& spec) {
          sdk::TrackPublishOptions publication;
          if (spec.capture == sdk::CaptureKind::camera) { publication.name = "摄像头"; publication.source = sdk::TrackSource::camera; }
          else if (spec.capture == sdk::CaptureKind::screen) { publication.name = "屏幕共享"; publication.source = sdk::TrackSource::screen_share; }
          else publication.name = spec.video ? "测试视频 " + std::to_string(++video_number) : "测试音调";
          room_.local_participant().publish_track(spec, publication).get();
        };
        for (const auto& spec : media) {
          if (stop.stop_requested()) break;
          publish(spec);
        }
        while (!stop.stop_requested() && room_.connection_state() != sdk::ConnectionState::disconnected) {
          if (room_.connection_state() == sdk::ConnectionState::reconnecting) {
            { std::lock_guard lock(mutex_); if (data_.state != State::Disconnecting) data_.state = State::Reconnecting; }
            std::this_thread::sleep_for(std::chrono::milliseconds(20)); continue;
          }
          std::optional<VideoCommand> command;
          std::optional<ChatMessage> chat;
          if (chat_send.valid() && chat_send.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            bool confirmed = true;
            try { chat_send.get(); } catch (const std::exception&) { confirmed = false; }
            std::lock_guard lock(mutex_); data_.chat.complete(sending_id, confirmed);
            data_.sending_chat = false; chat_in_flight_ = false;
            if (!confirmed) data_.chat_error = "发送结果未确认，请检查连接与发言权限。消息可能已送出，不会自动重发。";
          }
          if (!chat_send.valid()) { std::lock_guard lock(mutex_); chat.swap(chat_command_); if (chat) { chat_in_flight_ = true; data_.sending_chat = true; } }
          if (chat) {
            sending_id = chat->id;
            try {
              sdk::DataPublishOptions publish_options; publish_options.reliable = true; publish_options.topic = chat_topic;
              sdk::OperationOptions operation; operation.timeout = std::chrono::seconds(10); operation.stop = stop;
              chat_send = room_.local_participant().publish_data(encode_chat(chat->id, chat->text), publish_options, operation);
            } catch (const std::exception&) {
              std::lock_guard lock(mutex_); data_.chat.complete(sending_id, false); data_.sending_chat = false; chat_in_flight_ = false;
              data_.chat_error = "暂时无法发送，请检查连接与发言权限。";
            }
          }
          { std::lock_guard lock(mutex_); command.swap(video_command_); }
          std::string video_error;
          if (command) {
            try {
              if (command->microphone) { room_.local_participant().publish_microphone().get(); }
              else if (command->muted.has_value()) {
                bool found = false;
                for (const auto& track : room_.local_participant().track_publications())
                  if (track.sid() == command->remove_sid) { track.set_muted(*command->muted).get(); found = true; break; }
                if (!found) throw std::runtime_error("轨道已改变，请重试。");
              }
              else if (command->remove_sid.empty()) { sdk::MediaSpec spec; spec.demo = true; publish(command->source.value_or(spec)); }
              else room_.local_participant().unpublish_track(command->remove_sid).get();
            } catch (const std::exception& e) { video_error = redact(e.what()); }
          }
          auto local = room_.local_participant().info();
          std::vector<sdk::ParticipantInfo> remote;
          for (const auto& p : room_.remote_participants()) remote.push_back(p.info());
          const auto name = room_.info().name;
          { std::lock_guard lock(mutex_);
            data_.update(std::move(local), remote); data_.room = name;
            if (command) { data_.changing_video = false; data_.video_error = std::move(video_error); }
            if (data_.state != State::Disconnecting) data_.state = room_.connection_state() == sdk::ConnectionState::reconnecting ? State::Reconnecting : State::Connected;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (stop.stop_requested()) room_.request_stop();
        room_.wait();
        if (!stop.stop_requested()) error = "会议连接已结束（原因 " + std::to_string(room_.disconnect_reason()) + "），请重新加入。";
      } catch (const std::exception& e) { error = e.what(); }
      catch (...) { error = "会议发生未知错误"; }
      room_.disconnect();
      error = redact(std::move(error));
      std::lock_guard lock(mutex_);
      if (stop.stop_requested()) error.clear();
      data_.state = error.empty() ? State::Idle : State::Error;
      data_.error = std::move(error); data_.finished = true;
      data_.frames.clear(); data_.participants.clear(); data_.local_sid.clear();
      data_.changing_video = false; data_.video_error.clear(); video_command_.reset();
      data_.chat = {}; data_.sending_chat = false; data_.chat_error.clear(); chat_command_.reset(); chat_in_flight_ = false;
    });
  } catch (...) { std::lock_guard lock(mutex_); data_.finished = true; data_.state = State::Error; throw; }
}
void Session::stop() {
  { std::lock_guard lock(mutex_); if (data_.finished) return; data_.state = State::Disconnecting;
    data_.chat = {}; data_.sending_chat = false; data_.chat_error.clear(); chat_command_.reset(); }
  worker_.request_stop(); room_.request_stop();
}
bool Session::add_video() {
  sdk::MediaSpec spec; spec.demo = true; return add_video(spec);
}
bool Session::add_video(sdk::MediaSpec spec) {
  if (!spec.video) return false;
  std::lock_guard lock(mutex_);
  if (data_.state != State::Connected || data_.changing_video) return false;
  const auto local = data_.participants.find(data_.local_sid);
  if (local == data_.participants.end()) return false;
  const auto& tracks = local->second.tracks;
  if (std::count_if(tracks.begin(), tracks.end(), [](const auto& t) { return t.kind == sdk::TrackKind::video; }) >= max_test_videos) return false;
  video_command_ = VideoCommand{"", std::move(spec)}; data_.changing_video = true; data_.video_error.clear();
  return true;
}
bool Session::remove_video(const std::string& sid) {
  std::lock_guard lock(mutex_);
  if (data_.state != State::Connected || data_.changing_video) return false;
  const auto local = data_.participants.find(data_.local_sid);
  if (local == data_.participants.end()) return false;
  const auto& tracks = local->second.tracks;
  if (std::none_of(tracks.begin(), tracks.end(), [&](const auto& t) { return t.sid == sid && t.kind == sdk::TrackKind::video; })) return false;
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
