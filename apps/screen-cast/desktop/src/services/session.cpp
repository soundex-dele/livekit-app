#include "rtc_event_mailbox.hpp"
#include "livekit/rtc/livekit.hpp"
#include "services/session.hpp"
#include <av/http.hpp>
#include <livekit/rtc/engine.hpp>
#include "platform/permission.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>
using namespace std::chrono_literals;

namespace screen_cast {
namespace rtc = sdk::rtc;

Session::~Session() {
  stop();
  if (worker_.joinable())
    worker_.join();
}

Snapshot Session::snapshot() const {
  std::lock_guard lock(mutex_);
  return snapshot_;
}

void Session::stop() {
  stop_ = true;
  std::lock_guard lock(mutex_);
  snapshot_.preview.reset();
  if (!snapshot_.finished)
    snapshot_.state = "stopping";
}

void Session::start(std::string url, std::string code, sdk::ScreenCaptureDevice display) {
  if (!snapshot().finished)
    return;
  if (worker_.joinable())
    worker_.join();
  while (!url.empty() && url.back() == '/')
    url.pop_back();
  stop_ = false;
  {
    std::lock_guard lock(mutex_);
    snapshot_ = Snapshot{};
    snapshot_.state = "connecting";
    snapshot_.finished = false;
  }
  worker_ = std::thread([this, url = std::move(url), code = std::move(code),
                         display = std::move(display)] { run(url, code, display); });
}

void Session::run(std::string url, std::string code, sdk::ScreenCaptureDevice display) {
  std::string id, key;
  std::unique_ptr<rtc::engine::RtcEngine> engine;
  const auto setState = [&](std::string state) {
    std::lock_guard lock(mutex_);
    snapshot_.state = std::move(state);
  };
  try {
    if (code.size() != 10 ||
        !std::all_of(code.begin(), code.end(), [](char c) { return c >= '0' && c <= '9'; }))
      throw std::runtime_error("Enter a 10-digit cast code");
    if (!fixture_ && !stop_) {
      setState("permission");
      if (!(permission_ ? permission_() : requestScreenPermission()))
        throw std::runtime_error("Screen recording permission denied");
    }
    if (!fixture_ && !stop_) {
      auto devices = sdk::screen_capture_devices();
      if (std::find(devices.begin(), devices.end(), display) == devices.end())
        throw std::runtime_error("Display changed; refresh and select again");
    }
    if (!stop_) {
      setState("connecting");
      sdk::Json body{{"code", code}};
      auto v = av::request(url, "/api/v1/screen-cast/redeem", "", &body);
      id = v.at("id");
      key = v.at("credential");
      if (!stop_) {
        auto empty = sdk::Json::object();
        auto conn =
            av::request(url, "/api/v1/screen-cast/sessions/" + id + "/connect", key, &empty);
        if (!stop_) {
          engine = rtc::engine::create_livekit_engine({});
          livekit::sdk::app_support::RtcEventMailbox engine_events(*engine);
          const auto await = [&](auto& future) {
            while (future.wait_for(0ms) != std::future_status::ready) {
              if (stop_)
                throw std::runtime_error("Screen cast cancelled");

              std::this_thread::sleep_for(5ms);
            }
            if (stop_)
              throw std::runtime_error("Screen cast cancelled");
            return future.get();
          };
          rtc::LiveKitRoomOptions options;
          options.url = conn.at("media_url");
          options.token = conn.at("media_token");
          options.connect_timeout = 10;
          options.operation_timeout = 10s;
          options.reconnect.enabled = false;
          auto joining = engine->join(options);
          await(joining);
          const auto size = sdk::screen_capture_size(display.width, display.height);
          rtc::TrackDescriptor descriptor;
          descriptor.id = "screen";
          descriptor.name = "Screen";
          descriptor.source = rtc::PublicationSource::screen;
          descriptor.stream_group = "screen";
          descriptor.format.codec = rtc::Codec::vp8;
          descriptor.format.width = size.width;
          descriptor.format.height = size.height;
          rtc::engine::MediaInput input;
          input.fps = 15;
          input.generated = fixture_;
          if (!fixture_) {
            input.capture = sdk::CaptureKind::screen;
            input.screen_id = display.id;
          }
          rtc::engine::MediaPublishSpec config;
          config.track = descriptor;
          config.input = input;
          config.preview.video = [this](const auto& frame) {
            if (stop_)
              return;
            std::lock_guard lock(mutex_);
            if (!stop_)
              snapshot_.preview = frame.pixels;
          };
          auto publication = engine->publish_video(std::move(config));
          auto publishing = std::move(publication.ready);
          await(publishing);
          std::set<std::string> participants;
          if (auto state = engine->room().snapshot().session)
            for (const auto& [sid, p] : state->remote)
              participants.insert(sid);
          auto next = std::chrono::steady_clock::now();
          setState("paired");
          while (!stop_) {
            while (auto event = engine_events.take_event(0ms)) {
              if (event->kind == rtc::RoomEventKind::closed) {
                stop_ = true;
                break;
              }
              if (event->kind == rtc::RoomEventKind::failed)
                throw std::runtime_error(event->message.empty() ? "Media connection failed"
                                                                : event->message);
            }
            if (stop_)
              break;

            if (auto failure = engine_events.take_failure())
              throw std::runtime_error(failure->message);
            if (auto state = engine->room().snapshot().session) {
              std::set<std::string> current;
              for (const auto& [sid, p] : state->remote)
                current.insert(sid);
              if (std::any_of(participants.begin(), participants.end(),
                              [&](const auto& sid) { return !current.contains(sid); })) {
                stop_ = true;
                break;
              }
              participants = std::move(current);
            }
            if (std::chrono::steady_clock::now() >= next) {
              sdk::Json beat{{"connected", true}};
              v = av::request(url, "/api/v1/screen-cast/sessions/" + id + "/heartbeat", key, &beat);
              auto state = v.at("state").get<std::string>();
              setState(state);
              if (state == "ended" || state == "expired") {
                auto reason = v.value("reason", std::string{});
                if (reason != "stopped") {
                  std::lock_guard lock(mutex_);
                  snapshot_.error = reason;
                }
                break;
              }
              next = std::chrono::steady_clock::now() + 3s;
            }
            std::this_thread::sleep_for(5ms);
          }
        }
      }
    }
  } catch (const std::exception& e) {
    if (!stop_) {
      std::lock_guard lock(mutex_);
      snapshot_.error = e.what();
    }
  }
  // The supervisor closes its graph and capture before ending the business session.
  stop();
  if (engine) {
    try {
      engine->close().get();
    } catch (const std::exception& e) {
      std::lock_guard lock(mutex_);
      if (snapshot_.error.empty())
        snapshot_.error = e.what();
    }
    engine.reset();
  }
  if (!id.empty()) {
    try {
      auto empty = sdk::Json::object();
      av::request(url, "/api/v1/screen-cast/sessions/" + id + "/stop", key, &empty);
    } catch (const std::exception& e) {
      std::lock_guard lock(mutex_);
      if (snapshot_.error.empty())
        snapshot_.error = e.what();
    }
  }
  {
    std::lock_guard lock(mutex_);
    snapshot_.preview.reset();
    snapshot_.state = "ended";
    snapshot_.finished = true;
  }
}
}  // namespace screen_cast
