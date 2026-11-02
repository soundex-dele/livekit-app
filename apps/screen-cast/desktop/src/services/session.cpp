#include "rtc_blocking_bridge.hpp"
#include "rtc_event_mailbox.hpp"
#include "livekit/rtc/livekit.hpp"
#include "services/session.hpp"
#include <av/http.hpp>
#include <livekit/rtc/engine.hpp>
#include "platform/permission.hpp"
#include <algorithm>
#include <set>
#include <exception>
using namespace std::chrono_literals;

namespace screen_cast {
namespace rtc = sdk::rtc;

Session::~Session() {
  stop();
  if (worker_.joinable()) {
    worker_.join();
  }
}

Snapshot Session::snapshot() const {
  std::lock_guard lock(mutex_);
  return snapshot_;
}

void Session::stop() {
  stop_ = true;
  std::lock_guard lock(mutex_);
  snapshot_.preview.reset();
  if (!snapshot_.finished) {
    snapshot_.state = "stopping";
  }
}

void Session::start(std::string url, std::string code, sdk::ScreenCaptureDevice display) {
  if (!snapshot().finished) {
    return;
  }
  if (worker_.joinable()) {
    worker_.join();
  }
  while (!url.empty() && url.back() == '/') {
    url.pop_back();
  }
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
  std::unique_ptr<livekit::sdk::app_support::BlockingRtc> engine;
  const auto setState = [&](std::string state) {
    std::lock_guard lock(mutex_);
    snapshot_.state = std::move(state);
  };
  const auto failure = [](std::string message) {
    return sdk::Result<void>::failure(sdk::make_failure(
        sdk::ErrorDomain::lifecycle, sdk::ErrorCode::processing_failed, std::move(message)));
  };
  try {
    const auto work = [&]() -> sdk::Result<void> {
      if (code.size() != 10 ||
          !std::all_of(code.begin(), code.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return failure("Enter a 10-digit cast code");
      }
      if (!fixture_ && !stop_) {
        setState("permission");
        if (!(permission_ ? permission_() : requestScreenPermission())) {
          return failure("Screen recording permission denied");
        }
      }
      if (!fixture_ && !stop_) {
        auto devices = sdk::screen_capture_devices();
        if (!devices) {
          return sdk::Result<void>::failure(devices.failure_record());
        }
        if (std::find(devices->begin(), devices->end(), display) == devices->end()) {
          return failure("Display changed; refresh and select again");
        }
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
            auto created = livekit::sdk::app_support::create_blocking_engine(
                livekit::sdk::rtc::AudioOutputOptions{});
            if (!created) {
              return sdk::Result<void>::failure(created.failure_record());
            }
            engine = std::move(*created);
            livekit::sdk::app_support::RtcEventMailbox engine_events(*engine);
            auto mailbox = engine_events.status();
            if (!mailbox) {
              return mailbox;
            }
            rtc::LiveKitRoomOptions options;
            options.url = conn.at("media_url");
            options.token = conn.at("media_token");
            options.connect_timeout = 10;
            options.operation_timeout = 10s;
            options.reconnect.enabled = false;
            auto joining = engine->join(options);
            if (joining != sdk::ErrorCode::ok) {
              return sdk::Result<void>::failure(sdk::make_failure(
                  sdk::ErrorDomain::signaling, joining, "Media connection failed"));
            }
            const auto size = sdk::screen_capture_size(display.width, display.height);
            if (!size) {
              return sdk::Result<void>::failure(size.failure_record());
            }
            rtc::TrackDescriptor descriptor;
            descriptor.id = "screen";
            descriptor.name = "Screen";
            descriptor.source = rtc::PublicationSource::screen;
            descriptor.stream_group = "screen";
            descriptor.format.codec = rtc::Codec::vp8;
            descriptor.format.width = size->width;
            descriptor.format.height = size->height;
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
              if (stop_) {
                return sdk::Result<void>::success();
              }
              std::lock_guard lock(mutex_);
              if (!stop_) {
                snapshot_.preview = frame.pixels;
              }
              return sdk::Result<void>::success();
            };
            auto publication = engine->publish_video(std::move(config));
            if (!publication) {
              return sdk::Result<void>::failure(publication.failure_record());
            }

            std::set<std::string> participants;
            if (auto state = engine->room().snapshot().session) {
              for (const auto& [sid, p] : state->remote) {
                participants.insert(sid);
              }
            }
            auto next = std::chrono::steady_clock::now();
            setState("paired");
            while (!stop_) {
              for (;;) {
                auto received = engine_events.take_event(0ms);
                if (!received) {
                  return sdk::Result<void>::failure(received.failure_record());
                }
                if (!*received) {
                  break;
                }
                const auto& event = *received;
                if (event->kind == rtc::RoomEventKind::closed) {
                  stop_ = true;
                  break;
                }
                if (event->kind == rtc::RoomEventKind::failed) {
                  return failure(event->message.empty() ? "Media connection failed"
                                                        : event->message);
                }
              }
              if (stop_) {
                break;
              }

              auto graph_failed = engine_events.take_failure();
              if (!graph_failed) {
                return sdk::Result<void>::failure(graph_failed.failure_record());
              }
              if (*graph_failed) {
                return failure((**graph_failed).message);
              }
              if (auto state = engine->room().snapshot().session) {
                std::set<std::string> current;
                for (const auto& [sid, p] : state->remote) {
                  current.insert(sid);
                }
                if (std::any_of(participants.begin(), participants.end(),
                                [&](const auto& sid) { return !current.count(sid); })) {
                  stop_ = true;
                  break;
                }
                participants = std::move(current);
              }
              if (std::chrono::steady_clock::now() >= next) {
                sdk::Json beat{{"connected", true}};
                v = av::request(url, "/api/v1/screen-cast/sessions/" + id + "/heartbeat", key,
                                &beat);
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
      return sdk::Result<void>::success();
    };
    auto completed = work();
    if (!completed && !stop_) {
      std::lock_guard lock(mutex_);
      snapshot_.error = completed.error().message;
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
    auto closed = engine->close();
    if (closed != sdk::ErrorCode::ok) {
      std::lock_guard lock(mutex_);
      if (snapshot_.error.empty()) {
        snapshot_.error =
            "Media shutdown failed (code " + std::to_string(static_cast<int>(closed)) + ")";
      }
    }
    engine.reset();
  }
  if (!id.empty()) {
    try {
      auto empty = sdk::Json::object();
      av::request(url, "/api/v1/screen-cast/sessions/" + id + "/stop", key, &empty);
    } catch (const std::exception& e) {
      std::lock_guard lock(mutex_);
      if (snapshot_.error.empty()) {
        snapshot_.error = e.what();
      }
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
