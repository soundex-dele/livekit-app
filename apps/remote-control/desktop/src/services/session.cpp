#include "services/session.hpp"
#include "services/rtc_connection.hpp"
#include <algorithm>
#include <future>
#include <stdexcept>
#include <iostream>

using namespace std::chrono_literals;

namespace remote_app {
Session::Session(bool fixture, std::shared_ptr<sdk::ControlInputSink> sink) : fixture_(fixture) {
  if (fixture && !sink) {
    failure_ = sdk::make_failure(sdk::ErrorDomain::lifecycle, sdk::ErrorCode::invalid_argument,
                                 "Fixture requires recording input sink");
  } else if (!sink) {
    auto created = sdk::make_platform_control_input();
    if (!created) {
      failure_ = created.failure_record();
    } else {
      sink = *std::move(created);
    }
  }
  control_ = std::make_shared<sdk::RemoteControlSession>(std::move(sink));
  if (!failure_) {
    auto ready = control_->check();
    if (!ready) {
      failure_ = ready.failure_record();
    }
  }
  if (failure_) {
    snapshot_.error = failure_->message;
  }
}

Session::~Session() {
  stop();
  if (worker_.joinable())
    worker_.join();
}

Snapshot Session::snapshot() const {
  std::lock_guard lock(mutex_);
  return snapshot_;
}

void Session::start(std::string url, std::string account, std::string password,
                    std::string organization) {
  if (failure_) {
    return;
  }
  if (!snapshot().finished)
    return;
  if (worker_.joinable())
    worker_.join();
  while (!url.empty() && url.back() == '/')
    url.pop_back();
  if (url.empty())
    throw std::invalid_argument("Service URL required");
  stop_ = false;
  action_ = 0;
  {
    std::lock_guard lock(mutex_);
    snapshot_ = Snapshot{};
    snapshot_.state = "creating";
    snapshot_.finished = false;
  }
  organization_ = std::move(organization);
  worker_ = std::thread([this, url = std::move(url), account = std::move(account),
                         password = std::move(password)]() mutable {
    run(url, std::move(account), std::move(password));
  });
}

void Session::approve() {
  if (fixture_ || sdk::control_permissions().ready())
    action_ = 1;
}

void Session::reject() {
  action_ = 2;
}

void Session::stop() {
  stop_ = true;
  std::lock_guard lock(mutex_);
  control_->stop("local_stop");
}

void Session::update(const sdk::Json& v, sdk::ControlClock::time_point started) {
  std::lock_guard lock(mutex_);
  snapshot_.state = v.at("state");
  snapshot_.name = v.at("controller_name");
  snapshot_.expires = v.value("expires_at", "");
  grant_ = {v.at("grant_id"), v.at("controller_identity"), v.at("epoch"),
            started + std::chrono::milliseconds(v.at("lease_remaining_ms").get<int64_t>())};
}

void Session::run(std::string url, std::string account, std::string password) {
  std::string id;
  bool started_media = false;
  const auto work = [&]() -> sdk::Result<void> {
    auto empty = sdk::Json::object();
    sdk::Json login{{"name", account}, {"password", password}};
    auto auth = av::request_result(url, "/api/v1/auth/login", "", &login);
    std::fill(password.begin(), password.end(), '\0');
    login.clear();
    if (!auth) {
      return sdk::Result<void>::failure(auth.failure_record());
    }
    account_token_ = auth->value("session_token", std::string{});
    auto org = av::select_organization(url, account_token_, organization_);
    if (!org) {
      return sdk::Result<void>::failure(org.failure_record());
    }
    organization_ = *org;
    const av::RequestOptions options{organization_, ""};
    auto created = av::request_result(url, "/api/v1/av/remote-control/sessions", account_token_,
                                      &empty, options);
    if (!created) {
      return sdk::Result<void>::failure(created.failure_record());
    }
    id = created->value("id", std::string{});
    {
      std::lock_guard lock(mutex_);
      snapshot_.id = id;
      snapshot_.code = created->value("pairing_code", std::string{});
      snapshot_.state = "waiting";
    }
    auto next = sdk::ControlClock::now();
    auto next_presence = next;
    sdk::Json current;
    while (!stop_) {
      const auto now = sdk::ControlClock::now();
      const int action = action_.exchange(0);
      if (now >= next_presence) {
        auto beat =
            av::request_result(url, "/api/v1/av/remote-control/sessions/" + id + "/heartbeat",
                               account_token_, &empty, options);
        if (!beat && snapshot().state != "approved") {
          return sdk::Result<void>::failure(beat.failure_record());
        }
        next_presence = now + 3s;
      }
      if (action || now >= next) {
        auto request_started = sdk::ControlClock::now();
        sdk::Result<sdk::Json> response = av::request_result(
            url, "/api/v1/av/remote-control/sessions/" + id, account_token_, nullptr, options);
        if (!response) {
          // Transient API failures can retry only inside the existing local lease.
          std::lock_guard lock(mutex_);
          if (snapshot_.state == "approved" && request_started < grant_.deadline &&
              response.error().message.find("status 4") == std::string::npos) {
            next = request_started + 250ms;
            continue;
          }
          return sdk::Result<void>::failure(response.failure_record());
        }
        if (response->contains("control_grant")) {
          current = (*response)["control_grant"];
        }
        if (current.is_object()) {
          auto normalized = grant_view(current);
          const auto state = normalized.value("state", std::string{});
          const auto grant_id = normalized.value("id", std::string{});
          if (action || state == "approved") {
            if (action == 1 && !fixture_ && !sdk::control_permissions().ready()) {
              return sdk::Result<void>::failure(
                  sdk::make_failure(sdk::ErrorDomain::lifecycle, sdk::ErrorCode::request_failed,
                                    "请先授予屏幕录制和辅助功能权限"));
            }
            sdk::Json command{{"epoch", current.value("epoch", int64_t(0))}};
            if (action == 1) {
              command["local_consent"] = true;
            }
            const std::string verb = action == 1 ? "approve" : action == 2 ? "reject" : "renew";
            request_started = sdk::ControlClock::now();
            auto renewed =
                av::request_result(url, "/api/v1/av/control-grants/" + grant_id + "/" + verb,
                                   account_token_, &command, options);
            if (!renewed) {
              std::lock_guard lock(mutex_);
              if (verb == "renew" && request_started < grant_.deadline &&
                  renewed.error().message.find("status 4") == std::string::npos) {
                next = request_started + 250ms;
                continue;
              }
              return sdk::Result<void>::failure(renewed.failure_record());
            }
            if (verb == "renew") {
              normalized = grant_view(current);
              normalized["lease_remaining_ms"] = renewed->value("lease_remaining_ms", int64_t(0));
            } else {
              normalized = grant_view(*renewed);
            }
            if (normalized.value("state", std::string{}) == "approved") {
              update(normalized, request_started);
            }
          }
          {
            std::lock_guard lock(mutex_);
            snapshot_.state = normalized.value("state", std::string{});
            snapshot_.name = normalized.value("controller_name", std::string{});
          }
          if (snapshot().state == "approved" && !started_media) {
            started_media = true;
            rtc_ = std::thread([this, url, id] { media(url, id, account_token_); });
          } else if (snapshot().state != "approved" && snapshot().state != "pending") {
            break;
          }
        }
        next = sdk::ControlClock::now() + (snapshot().state == "approved" ? 3s : 1s);
      }
      std::this_thread::sleep_for(20ms);
    }
    return sdk::Result<void>::success();
  };
  const auto outcome = work();
  if (!outcome && !stop_) {
    std::lock_guard lock(mutex_);
    snapshot_.error = outcome.error().message;
  }
  stop();
  if (rtc_.joinable()) {
    rtc_.join();
  }
  if (!id.empty()) {
    auto empty = sdk::Json::object();
    const auto ended = av::request_result(url, "/api/v1/av/remote-control/sessions/" + id + "/stop",
                                          account_token_, &empty, {organization_, ""});
    if (!ended) {
      std::lock_guard lock(mutex_);
      if (snapshot_.error.empty()) {
        snapshot_.error = ended.error().message;
      }
    }
  }
  account_token_.clear();
  {
    std::lock_guard lock(mutex_);
    snapshot_.state = "ended";
    snapshot_.finished = true;
  }
}

void Session::media(std::string url, std::string id, std::string credential) {
  // Fixture diagnostics contain timing and envelope metadata only, never text,
  // input contents, credentials or media tokens. Production does not emit them.
  auto trace = [enabled = fixture_, began = sdk::ControlClock::now()](
                   const char* event, sdk::Json fields = sdk::Json::object()) {
    if (!enabled)
      return;
    fields["event"] = event;
    fields["elapsed_ms"] =
        std::chrono::duration_cast<std::chrono::milliseconds>(sdk::ControlClock::now() - began)
            .count();
    static std::mutex trace_mutex;
    std::lock_guard lock(trace_mutex);
    std::cerr << fields.dump() << '\n';
  };
  std::unique_ptr<RtcConnection> connection_media;
  try {
    const auto run_media = [&]() -> sdk::Result<void> {
      sdk::Json empty = sdk::Json::object();
      auto access = av::request_result(url, "/api/v1/av/remote-control/sessions/" + id + "/connect",
                                       credential, &empty, {organization_, ""});
      if (!access) {
        return sdk::Result<void>::failure(access.failure_record());
      }
      auto media_access = av::media_credentials(url, access->value("grant_id", std::string{}),
                                                credential, {organization_, ""}, &stop_);
      if (!media_access) {
        return sdk::Result<void>::failure(media_access.failure_record());
      }
      auto connection = *media_access;
      if (stop_)
        return sdk::Result<void>::success();
      sdk::ControlGrant grant;
      {
        std::lock_guard lock(mutex_);
        grant = grant_;
      }
      connection_media =
          std::make_unique<RtcConnection>(connection.at("media_url"), connection.at("media_token"),
                                          grant.controller_identity, false, stop_);
      auto ready = connection_media->status();
      if (!ready) {
        return ready;
      }
      auto layout_result =
          fixture_
              ? sdk::Result<sdk::ControlLayout>::success(sdk::ControlLayout{1, 0, 0, 1280, 720})
              : sdk::primary_control_layout();
      if (!layout_result) {
        return sdk::Result<void>::failure(layout_result.failure_record());
      }
      auto layout = *layout_result;
      const auto height =
          std::max(2, static_cast<int>(1280.0 * layout.height / layout.width) / 2 * 2);
      auto published = connection_media->publish_screen(1280, height, fixture_);
      if (!published) {
        return published;
      }
      trace("room_joined");
      {
        std::lock_guard lock(mutex_);
        if (stop_)
          return sdk::Result<void>::success();
        grant = grant_;
        auto authorized = control_->authorize(grant, layout);
        if (!authorized) {
          return authorized;
        }
      }
      auto next = sdk::ControlClock::now();
      while (!stop_) {
        auto polled = connection_media->poll();
        if (!polled) {
          return polled;
        }
        auto packets = connection_media->take_data();
        if (!packets) {
          return sdk::Result<void>::failure(packets.failure_record());
        }
        {
          std::lock_guard lock(mutex_);
          grant = grant_;
        }
        if (!control_->renew(grant)) {
          return sdk::Result<void>::failure(sdk::make_failure(
              sdk::ErrorDomain::lifecycle, sdk::ErrorCode::request_failed, control_->reason()));
        }
        for (auto& [bytes, params] : *packets) {
          trace("data_received",
                {{"bytes", bytes.size()}, {"topic", params.topic}, {"reliable", params.reliable}});
          auto accepted = control_->receive(bytes, params);
          trace("control_packet", {{"accepted", accepted}, {"active", control_->active()}});
        }
        if (!control_->active()) {
          if (control_->reason() == "peer_stopped") {
            stop_ = true;
            break;
          }
          return sdk::Result<void>::failure(sdk::make_failure(
              sdk::ErrorDomain::lifecycle, sdk::ErrorCode::request_failed, control_->reason()));
        }
        if (sdk::ControlClock::now() >= next) {
          if (!fixture_) {
            if (!sdk::control_permissions().ready())
              return sdk::Result<void>::failure(sdk::make_failure(sdk::ErrorDomain::lifecycle,
                                                                  sdk::ErrorCode::request_failed,
                                                                  "权限或交互会话已失效"));
            auto measured = sdk::primary_control_layout();
            if (!measured) {
              return sdk::Result<void>::failure(measured.failure_record());
            }
            auto current = *measured;
            current.version = layout.version;
            if (current != layout) {
              current.version++;
              layout = current;
              auto updated = control_->set_layout(layout);
              if (!updated) {
                return updated;
              }
            }
          }
          auto raw = control_->status().dump();
          auto sent = connection_media->send({raw.begin(), raw.end()});
          if (!sent) {
            return sent;
          }
          trace("status_sent");
          next = sdk::ControlClock::now() + 500ms;
        }
        std::this_thread::sleep_for(10ms);
      }
      control_->stop("media_ended");
      connection_media->close();
      return sdk::Result<void>::success();
    };
    const auto outcome = run_media();
    if (!outcome) {
      const bool cancelled = stop_.exchange(true);
      control_->stop("media_error");
      if (!cancelled && outcome.error().code != sdk::ErrorCode::session_closed) {
        std::lock_guard lock(mutex_);
        snapshot_.error = outcome.failure_record()->message;
      }
    } else {
      stop_ = true;
    }
  } catch (const std::exception& e) {
    const bool cancelled = stop_.exchange(true);
    trace("media_failed", {{"reason", control_->reason()}});
    control_->stop("media_error");
    if (!cancelled) {
      std::lock_guard lock(mutex_);
      snapshot_.error = e.what();
    }
  }
  // Release held input before any potentially blocking graph/transport teardown.
  control_->stop("media_ended");
  if (connection_media) {
    connection_media->close();
  }
}
}  // namespace remote_app
