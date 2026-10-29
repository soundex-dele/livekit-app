#include "livekit/rtc/livekit.hpp"
#include "session.hpp"
#include <av/http.hpp>
#include "platform.hpp"
#include <fstream>
#include <algorithm>

namespace studio_app {
namespace {
const std::string prefix = "/api/v1/live-studio/";
}

Session::Session(std::filesystem::path settings, sdk::StudioOptions options, bool offline)
    : settings_(std::move(settings)), engine_(std::make_shared<sdk::Studio>(options)) {
  try {
    auto account = offline ? Json() : platform::load_account(settings_);
    if (!account.is_null()) {
      url_ = account.at("url");
      token_ = account.at("token");
    }
  } catch (const std::exception& e) {
    state_.error = e.what();
  }
  state_.server_url = url_;
  worker_ = std::jthread([this] { run(); });
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
  if (commands_.size() < 32)
    commands_.push_back(std::move(f));
  else
    state_.error = "操作队列已满，请稍后重试";
}

Json Session::request(const std::string& path, const Json* body) {
  return av::request(url_, path, token_, body);
}

void Session::store_token() {
  platform::save_account(settings_, Json{{"url", url_}, {"token", token_}});
}

void Session::login(std::string url, std::string name, std::string password, bool registration) {
  enqueue([this, url = std::move(url), name = std::move(name), password = std::move(password),
           registration]() mutable {
    if (!broadcast_.empty())
      throw std::runtime_error("请先结束直播");
    if (!url.starts_with("https://") && !url.starts_with("http://127.0.0.1:") &&
        !url.starts_with("http://localhost:"))
      throw std::runtime_error("公网服务请使用 HTTPS");
    while (url.ends_with('/'))
      url.pop_back();
    url_ = url;
    token_.clear();
    Json body{{"name", name}, {"password", password}};
    auto result =
        request(std::string("/api/v1/auth/") + (registration ? "register" : "login"), &body);
    std::fill(password.begin(), password.end(), '\0');
    token_ = result.at("session_token");
    store_token();
    {
      std::lock_guard lock(mutex_);
      state_.server_url = url_;
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
    auto result = request(prefix + "start", &c);
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
        request(prefix + "broadcasts/" + broadcast_ + "/stop", &empty);
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
    request(prefix + "broadcasts/" + broadcast_ + "/stop", &empty);
    disconnect();
  });
}

void Session::command(std::string action, Json payload) {
  enqueue([this, action = std::move(action), payload = std::move(payload)] {
    if (broadcast_.empty())
      throw std::runtime_error("当前没有直播");
    request(prefix + "broadcasts/" + broadcast_ + "/" + action, &payload);
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
      next->set_scene(scene_);
      next->set_audio(audio_);
      old = std::move(engine_);
      engine_ = std::move(next);
    }
  });
}

void Session::set_scene(sdk::StudioScene s) {
  std::lock_guard lock(mutex_);
  engine_->set_scene(s);
  scene_ = std::move(s);
}

void Session::set_audio(sdk::StudioAudioSettings a) {
  std::lock_guard lock(mutex_);
  engine_->set_audio(a);
  audio_ = a;
}

void Session::connect(bool interaction) {
  Json args{{"stage", interaction}};
  auto credentials = request(prefix + "broadcasts/" + broadcast_ + "/connect", &args);
  sdk::rtc::LiveKitRoomOptions options;
  options.url = credentials.at("media_url");
  options.token = credentials.at("media_token");
  options.connect_timeout = 10;
  options.operation_timeout = std::chrono::seconds(10);
  options.stats_interval = std::chrono::seconds(1);
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
          frames += row.value("framesEncoded", uint64_t(0));
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
    events_->stop();
    events_.reset();
  }
  auto ws = std::make_unique<ix::WebSocket>();
  auto address = url_;
  if (address.starts_with("https:"))
    address.replace(0, 5, "wss");
  else
    address.replace(0, 4, "ws");
  ws->setUrl(address + prefix + "broadcasts/" + broadcast_ +
             "/events?after=" + std::to_string(sequence_));
  ws->setExtraHeaders({{"Authorization", "Bearer " + token_}});
  ws->setOnMessageCallback([this](const ix::WebSocketMessagePtr& m) {
    if (m->type == ix::WebSocketMessageType::Message) {
      try {
        auto value = Json::parse(m->str);
        std::lock_guard lock(mutex_);
        if (incoming_.size() < 64)
          incoming_.push_back(std::move(value));
      } catch (...) {
      }
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
      stage_key_ = key;
    }
  }
}

void Session::run() {
  auto heartbeat = std::chrono::steady_clock::now(), refresh = heartbeat;
  try {
    if (!token_.empty()) {
      auto u = request("/api/v1/auth/me");
      std::lock_guard lock(mutex_);
      state_.user = u;
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
      state_.busy = false;
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
      if (!broadcast_.empty() && now >= heartbeat) {
        heartbeat = now + std::chrono::seconds(3);
        bool connected = program_ && program_->connected();
        Json c{{"connected", connected}};
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
      request(prefix + "broadcasts/" + broadcast_ + "/stop", &empty);
    } catch (...) {
    }
  }
  disconnect();
}
}  // namespace studio_app
