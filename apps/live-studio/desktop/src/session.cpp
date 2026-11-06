#include "livekit/compat/algorithm.hpp"
#include "livekit/compat/thread.hpp"
#include "session.hpp"
#include <av/http.hpp>
#include "platform.hpp"
#include <fstream>
#include <algorithm>
#include <map>

namespace studio_app {
namespace {
const std::string prefix = "/api/v1/av/live-studio/";
}

Session::Session(std::filesystem::path settings, sdk::StudioOptions options, bool offline)
    : settings_(std::move(settings)), engine_(std::make_shared<sdk::Studio>(options)) {
  try {
    auto account = offline ? Json() : platform::load_account(settings_);
    if (!account.is_null()) {
      url_ = account.at("url");
      token_ = account.at("token");
      organization_ = account.value("organization", std::string{});
    }
  } catch (const std::exception& e) {
    state_.error = e.what();
  }
  state_.server_url = url_;
  state_.organization = organization_;
  worker_ = livekit::compat::jthread([this] { run(); });
}

Session::~Session() {
  closing_ = true;
  if (worker_.joinable())
    worker_.join();
}

std::shared_ptr<sdk::Studio> Session::engine() const {
  std::lock_guard lock(mutex_);
  return engine_;
}

Snapshot Session::snapshot() const {
  std::lock_guard lock(mutex_);
  return state_;
}

void Session::enqueue(std::function<void()> f) {
  std::lock_guard lock(mutex_);
  if (commands_.size() < 32) {
    commands_.push_back(std::move(f));
    // Busy includes queued work so the UI cannot resubmit while the worker is
    // still finishing its periodic refresh before taking this command.
    state_.busy = true;
  } else {
    state_.error = "操作队列已满，请稍后重试";
  }
}

Json Session::request(const std::string& path, const Json* body) {
  const av::RequestOptions options{organization_, ""};
  auto result = av::request(url_, path, token_, body, options);
  if (path == prefix + "rooms" && !body) {
    auto rooms = result.value("items", Json::array());
    for (auto& room : rooms) {
      room["broadcast"] = room.value("broadcast_id", std::string{});
    }
    return rooms;
  }
  if (path.size() >= 10 && path.substr(path.size() - 10) == "/heartbeat") {
    result = av::request(url_, path.substr(0, path.size() - 10), token_, nullptr, options);
    result["broadcast"]["room"] = result.value("room_id", std::string{});
    result["broadcast"]["owner"] = result.value("host", std::string{});
    result["guests"] = Json::array();
    for (const auto& peer : result.value("stage_members", Json::array())) {
      result["guests"].push_back(
          Json{{"user_id", peer.value("subject_id", std::string{})},
               {"name", peer.value("name", peer.value("subject_id", std::string{}))},
               {"state", peer.value("stage", std::string{})}});
    }
    result["viewers"] = Json::array();
    for (const auto& peer : result.value("members", Json::array())) {
      result["viewers"].push_back(
          Json{{"user_id", peer.value("subject_id", std::string{})},
               {"identity", peer.value("subject_id", std::string{})},
               {"name", peer.value("name", peer.value("subject_id", std::string{}))},
               {"muted", peer.value("muted", false)}});
    }
    if (result.contains("pk") && result["pk"].is_object()) {
      auto& pk = result["pk"];
      pk["a"] = pk.value("left_id", std::string{});
      pk["b"] = pk.value("right_id", std::string{});
      pk["score_a"] = pk.value("left_score", int64_t(0));
      pk["score_b"] = pk.value("right_score", int64_t(0));
      if (pk.value("state", std::string{}) == "pending") {
        pk["state"] = "invited";
      }
    }
  }
  return result;
}

void Session::store_token() {
  platform::save_account(settings_,
                         Json{{"url", url_}, {"token", token_}, {"organization", organization_}});
}

void Session::login(std::string url, std::string name, std::string password, bool registration,
                    std::string organization) {
  enqueue([this, url = std::move(url), name = std::move(name), password = std::move(password),
           registration, organization = std::move(organization)]() mutable {
    if (!broadcast_.empty())
      throw std::runtime_error("请先结束直播");
    if (!livekit::compat::starts_with(url, "https://") &&
        !livekit::compat::starts_with(url, "http://127.0.0.1:") &&
        !livekit::compat::starts_with(url, "http://localhost:"))
      throw std::runtime_error("公网服务请使用 HTTPS");
    while (livekit::compat::ends_with(url, '/'))
      url.pop_back();
    url_ = url;
    token_.clear();
    Json body{{"name", name}, {"password", password}};
    auto result =
        request(std::string("/api/v1/auth/") + (registration ? "register" : "login"), &body);
    std::fill(password.begin(), password.end(), '\0');
    token_ = result.at("session_token");
    auto selected = av::select_organization(url_, token_, organization);
    if (!selected) {
      std::lock_guard lock(mutex_);
      state_.error = selected.error().message;
      return;
    }
    organization_ = *selected;
    store_token();
    {
      std::lock_guard lock(mutex_);
      state_.server_url = url_;
      state_.organization = organization_;
      state_.user = result.at("user");
      state_.error.clear();
    }
    refresh_rooms();
  });
}

void Session::logout() {
  enqueue([this] {
    disconnect();
    Json empty = Json::object();
    request("/api/v1/auth/logout", &empty);
    token_.clear();
    store_token();
    std::lock_guard lock(mutex_);
    state_.user = nullptr;
  });
}

void Session::change_password(std::string old, std::string password) {
  enqueue([this, old = std::move(old), password = std::move(password)] {
    Json body{{"old_password", old}, {"password", password}};
    request("/api/v1/auth/password", &body);
    disconnect();
    token_.clear();
    store_token();
    std::lock_guard lock(mutex_);
    state_.user = nullptr;
    state_.state = "密码已更新，请重新登录";
  });
}

void Session::start(std::string title) {
  enqueue([this, title = std::move(title)] {
    if (!broadcast_.empty())
      return;
    if (token_.empty())
      throw std::runtime_error("请先登录");
    Json c{{"title", title}};
    auto room = request(prefix + "rooms", &c);
    auto result = request(prefix + "rooms/" + room.value("id", std::string{}) + "/start", &c);
    broadcast_ = result.at("id");
    sequence_ = 0;
    {
      std::lock_guard lock(mutex_);
      state_.broadcast = broadcast_;
      state_.state = "连接直播服务…";
      state_.events = Json::array();
    }
    try {
      connect(false);
      events_start();
    } catch (...) {
      try {
        Json empty = Json::object();
        request(prefix + "broadcasts/" + broadcast_ + "/end", &empty);
      } catch (...) {
      }
      disconnect();
      throw;
    }
  });
}

void Session::stop() {
  enqueue([this] {
    if (broadcast_.empty())
      return;
    Json empty = Json::object();
    request(prefix + "broadcasts/" + broadcast_ + "/end", &empty);
    disconnect();
  });
}

void Session::command(std::string action, Json payload) {
  enqueue([this, action = std::move(action), payload = std::move(payload)] {
    if (broadcast_.empty())
      throw std::runtime_error("当前没有直播");
    const std::map<std::string, std::string> actions{{"guest/approve", "stage-approve"},
                                                     {"guest/reject", "stage-reject"},
                                                     {"guest/end", "stage-remove"},
                                                     {"pk/invite", "pk-request"},
                                                     {"pk/accept", "pk-accept"},
                                                     {"pk/reject", "pk-reject"},
                                                     {"pk/leave", "pk-end"}};
    const auto translated = actions.find(action);
    request(prefix + "broadcasts/" + broadcast_ + "/" +
                (translated == actions.end() ? action : translated->second),
            &payload);
  });
}

void Session::refresh_rooms() {
  enqueue([this] {
    auto rooms = request(prefix + "rooms");
    std::lock_guard lock(mutex_);
    state_.rooms = std::move(rooms);
  });
}

void Session::reconfigure(sdk::StudioOptions o) {
  enqueue([this, o] {
    if (!broadcast_.empty() || engine()->snapshot().recording)
      throw std::runtime_error("开播或录制时不能改变画布尺寸");
    auto next = std::make_shared<sdk::Studio>(o);
    std::shared_ptr<sdk::Studio> old;
    {
      std::lock_guard lock(mutex_);
      for (const auto& result :
           {next->status(), next->set_scene(scene_), next->set_audio(audio_)}) {
        if (!result) {
          state_.error = result.error().message;
          return;
        }
      }
      old = std::move(engine_);
      engine_ = std::move(next);
    }
  });
}

void Session::set_scene(sdk::StudioScene s) {
  std::lock_guard lock(mutex_);
  auto result = engine_->set_scene(s);
  if (!result) {
    state_.error = result.error().message;
    return;
  }
  scene_ = std::move(s);
}

void Session::set_audio(sdk::StudioAudioSettings a) {
  std::lock_guard lock(mutex_);
  auto result = engine_->set_audio(a);
  if (!result) {
    state_.error = result.error().message;
    return;
  }
  audio_ = a;
}

void Session::connect(bool interaction) {
  Json args{{"stage", interaction}};
  auto access = request(prefix + "broadcasts/" + broadcast_ + "/connect", &args);
  auto media = av::media_credentials(url_, access.value("grant_id", std::string{}), token_,
                                     {organization_, ""}, &closing_);
  if (!media) {
    std::lock_guard lock(mutex_);
    state_.error = media.error().message;
    return;
  }
  auto credentials = *media;
  sdk::client::ConnectionOptions options;
  options.url = credentials.at("media_url");
  options.token = credentials.at("media_token");
  options.connect_timeout = 10;
  options.operation_timeout = std::chrono::seconds(10);
  options.reconnect.enabled = false;
  options.max_tracks = 16;
  MediaSession::Statistics stats;
  if (!interaction)
    stats = [this, previous = std::make_shared<std::array<uint64_t, 3>>()](const auto& report) {
      if (report.target != 0)
        return;
      auto rows = Json::parse(report.json);
      uint64_t bytes = 0, frames = 0;
      Json media = Json::array();
      for (const auto& row : rows) {
        auto type = row.value("type", "");
        if (type == "outbound-rtp" || type == "media-source")
          media.push_back(row);
        if (type == "outbound-rtp") {
          bytes += row.value("bytesSent", uint64_t(0));
          frames += row.value("framesSent", row.value("framesEncoded", uint64_t(0)));
        }
      }
      auto& lastBytes = (*previous)[0];
      auto& lastFrames = (*previous)[1];
      auto& lastTime = (*previous)[2];
      auto time = uint64_t(report.timestamp_us);
      std::lock_guard lock(mutex_);
      state_.media_stats = std::move(media);
      if (lastTime && time > lastTime && bytes >= lastBytes && frames >= lastFrames) {
        double seconds = double(time - lastTime) / 1e6;
        state_.bitrate_kbps = double(bytes - lastBytes) * 8 / seconds / 1000;
        state_.publish_fps = double(frames - lastFrames) / seconds;
      }
      lastBytes = bytes;
      lastFrames = frames;
      lastTime = time;
    };
  auto connection = std::make_unique<MediaSession>(std::move(options), engine(), interaction,
                                                   closing_, std::move(stats));
  if (!connection->error().empty()) {
    std::lock_guard lock(mutex_);
    state_.error = connection->error();
    return;
  }
  if (interaction)
    stage_ = std::move(connection);
  else {
    program_ = std::move(connection);
    std::lock_guard lock(mutex_);
    state_.connected = true;
    state_.state = "等待服务端确认发布";
  }
}

void Session::close_stage() {
  stage_.reset();
  stage_key_.clear();
}

void Session::disconnect() {
  if (events_) {
    events_->stop();
    events_.reset();
  }
  close_stage();
  program_.reset();
  broadcast_.clear();
  sequence_ = 0;
  std::lock_guard lock(mutex_);
  state_.connected = false;
  state_.broadcast.clear();
  state_.state = "未开播";
  state_.live = nullptr;
  state_.bitrate_kbps = state_.publish_fps = 0;
  state_.media_stats = Json::array();
}

void Session::events_start() {
  if (events_) {
    events_->setOnMessageCallback({});
    events_->stop();
    events_.reset();
  }
  Json body{{"resource_id", broadcast_}};
  auto ticket = request("/api/v1/av/realtime/tickets", &body);
  auto ws = std::make_unique<ix::WebSocket>();
  auto address = url_;
  if (livekit::compat::starts_with(address, "https:")) {
    address.replace(0, 5, "wss");
  } else {
    address.replace(0, 4, "ws");
  }
  ws->setUrl(address + "/api/v1/av/realtime/ws?ticket=" + ticket.value("ticket", std::string{}) +
             "&after=" + std::to_string(sequence_));
  ws->disableAutomaticReconnection();
  ws->setOnMessageCallback([this](const ix::WebSocketMessagePtr& message) {
    if (message->type != ix::WebSocketMessageType::Message || closing_) {
      return;
    }
    auto value = Json::parse(message->str, nullptr, false);
    if (value.is_discarded()) {
      return;
    }
    auto batch = value.is_array()          ? value
                 : value.contains("items") ? value["items"]
                                           : Json::array({value});
    for (auto& event : batch) {
      event["seq"] = event.value("sequence", int64_t(0));
      auto type = event.value("type", std::string{});
      event["kind"] = livekit::compat::starts_with(type, "live.") ? type.substr(5) : type;
      if (event.contains("payload")) {
        auto& payload = event["payload"];
        payload["name"] = payload.value("name", payload.value("subject_id", std::string{}));
        payload["text"] = payload.value("content", std::string{});
        payload["points"] = payload.value("amount", int64_t(0));
      }
    }
    std::lock_guard lock(mutex_);
    if (incoming_.size() < 64) {
      incoming_.push_back(std::move(batch));
    }
  });
  ws->start();
  events_ = std::move(ws);
}

void Session::update_stage() {
  Json live;
  {
    std::lock_guard lock(mutex_);
    live = state_.live;
  }
  if (live.is_null())
    return;
  std::string key;
  auto pk = live.value("pk", Json());
  if (pk.is_object() && pk.value("state", "") == "active")
    key = "pk-" + pk.at("id").get<std::string>();
  else
    for (auto& g : live.value("guests", Json::array()))
      if (g.value("state", "") == "approved") {
        key = "guests";
        break;
      }
  if (stage_ && stage_->finished())
    close_stage();
  if (key != stage_key_) {
    close_stage();
    if (!key.empty()) {
      connect(true);
      if (stage_) {
        stage_key_ = key;
      }
    }
  }
}

void Session::run() {
  auto heartbeat = std::chrono::steady_clock::now(), refresh = heartbeat, event_retry = heartbeat;
  try {
    if (!token_.empty()) {
      auto selected = av::select_organization(url_, token_, organization_);
      if (selected) {
        organization_ = *selected;
      }
      auto u = request("/api/v1/auth/me");
      std::lock_guard lock(mutex_);
      state_.user = u;
      state_.organization = organization_;
    }
  } catch (...) {
    token_.clear();
  }
  while (!closing_) {
    std::function<void()> cmd;
    {
      std::lock_guard lock(mutex_);
      if (!commands_.empty()) {
        cmd = std::move(commands_.front());
        commands_.pop_front();
        state_.busy = true;
        state_.error.clear();
      }
    }
    if (cmd) {
      try {
        cmd();
      } catch (const std::exception& e) {
        std::lock_guard lock(mutex_);
        state_.error = e.what();
      }
      std::lock_guard lock(mutex_);
      state_.busy = !commands_.empty();
    }
    try {
      {
        std::lock_guard lock(mutex_);
        state_.stage_video_frames = stage_ ? stage_->received_video() : 0;
        state_.stage_audio_frames = stage_ ? stage_->received_audio() : 0;
        state_.stage_subscriptions = stage_ ? stage_->subscriptions() : 0;
      }
      for (const auto* media : {program_.get(), stage_.get()})
        if (media) {
          auto error = media->error();
          if (!error.empty()) {
            std::lock_guard lock(mutex_);
            state_.error = error;
          }
        }
      {
        std::lock_guard lock(mutex_);
        while (!incoming_.empty()) {
          auto batch = std::move(incoming_.front());
          incoming_.pop_front();
          for (auto& e : batch) {
            auto seq = e.value("seq", int64_t(0));
            if (seq <= sequence_)
              continue;
            sequence_ = seq;
            state_.events.push_back(e);
            while (state_.events.size() > 200)
              state_.events.erase(state_.events.begin());
          }
        }
      }
      auto now = std::chrono::steady_clock::now();
      if (!broadcast_.empty() && now >= event_retry &&
          (!events_ || events_->getReadyState() == ix::ReadyState::Closed)) {
        event_retry = now + std::chrono::seconds(5);
        events_start();
      }
      if (!broadcast_.empty() && now >= heartbeat) {
        heartbeat = now + std::chrono::seconds(3);
        bool connected = program_ && program_->connected();
        Json c = Json::object();
        auto live = request(prefix + "broadcasts/" + broadcast_ + "/heartbeat", &c);
        {
          std::lock_guard lock(mutex_);
          state_.live = live;
          state_.connected = connected;
          state_.state = live.at("broadcast").value("state", "") == "live" ? "直播中" : "重连中";
        }
        if (live.at("broadcast").value("state", "") == "ended") {
          disconnect();
        } else {
          if (!program_ || program_->finished()) {
            program_.reset();
            connect(false);
          }
          update_stage();
        }
      }
      if (!token_.empty() && now >= refresh) {
        refresh = now + std::chrono::minutes(10);
        Json empty = Json::object();
        auto auth = request("/api/v1/auth/refresh", &empty);
        token_ = auth.at("session_token");
        store_token();
        if (!broadcast_.empty())
          events_start();
      }
    } catch (const std::exception& e) {
      std::lock_guard lock(mutex_);
      state_.error = e.what();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (!broadcast_.empty()) {
    try {
      Json empty = Json::object();
      request(prefix + "broadcasts/" + broadcast_ + "/end", &empty);
    } catch (...) {
    }
  }
  disconnect();
}
}  // namespace studio_app
