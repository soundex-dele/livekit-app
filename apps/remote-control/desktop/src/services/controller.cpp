#include "services/rtc_connection.hpp"
#include "controller.hpp"
#include <algorithm>
#include <future>
#include <stdexcept>
using namespace std::chrono_literals;

namespace remote_app {
void ControlProtocol::push(sdk::Json e) {
  if (e.at("type") == "move") {
    move = std::move(e);
    return;
  }
  flush_move();
  if (queue.size() >= 256)
    throw std::runtime_error("控制消息积压，连接已停止");
  queue.push_back(std::move(e));
}

void ControlProtocol::flush_move() {
  if (move) {
    if (queue.size() >= 256)
      throw std::runtime_error("控制消息积压");
    queue.push_back(std::move(*move));
    move.reset();
  }
}

sdk::Json ControlProtocol::next() {
  auto e = std::move(queue.front());
  queue.pop_front();
  e["v"] = 1;
  e["grant_id"] = grant.at("grant_id");
  e["epoch"] = grant.at("epoch");
  e["seq"] = ++seq;
  if (seq > 9007199254740991ULL)
    throw std::runtime_error("控制序号耗尽");
  return e;
}

bool ControlProtocol::receive(const std::vector<uint8_t>& bytes, const sdk::DataReceiveParams& p) {
  if (!p.reliable || p.topic != sdk::remote_control_topic ||
      p.sender_identity != grant.value("host_identity", "") || bytes.size() > 4096)
    return false;
  sdk::Json v;
  try {
    v = sdk::Json::parse(bytes);
    if (v.at("v") != 1 || v.at("type") != "layout" || v.at("grant_id") != grant.at("grant_id") ||
        v.at("epoch") != grant.at("epoch"))
      return false;
    if (!v.at("layout_version").is_number_unsigned() || v.at("layout_version") == 0 ||
        v.at("layout_version") > 9007199254740991ULL || !v.at("width").is_number_integer() ||
        !v.at("height").is_number_integer() || v.at("width") < 1 || v.at("width") > 32768 ||
        v.at("height") < 1 || v.at("height") > 32768 || !v.at("active").is_boolean())
      return false;
    if (!layout.is_null() && v.at("layout_version") < layout.at("layout_version"))
      return false;
    if (v.contains("text_input") && !v["text_input"].is_boolean())
      return false;
  } catch (const sdk::Json::exception&) {
    return false;
  }
  if (!v.at("active").get<bool>())
    throw std::runtime_error("被控端已停止控制");
  if (pending && v.contains("text_result")) {
    auto r = v["text_result"];
    if (r.is_object() && r.value("id", uint64_t(0)) == pending && r.contains("accepted") &&
        r["accepted"].is_boolean()) {
      if (r["accepted"].get<bool>()) {
        accepted = pending;
        text_status = "已送入远端输入系统，请观察目标应用";
      } else
        text_status = "远端拒绝文字：" + r.value("reason", "");
      pending = 0;
    }
  }
  bool changed = layout.is_null() || v.at("layout_version") != layout.at("layout_version");
  if (changed) {
    queue.clear();
    move.reset();
    push({{"type", "heartbeat"}});
    push({{"type", "release"}});
    push({{"type", "layout_ack"}, {"layout_version", v.at("layout_version")}});
  }
  layout = std::move(v);
  return true;
}

void ControlProtocol::text(std::string value, sdk::ControlClock::time_point now) {
  if (pending)
    throw std::runtime_error("请等待上一条文字确认");
  if (layout.is_null() || !layout.value("text_input", false))
    throw std::runtime_error("当前被控端不支持发送文字");
  auto q = QString::fromUtf8(value);
  if (q.toUtf8().toStdString() != value)
    throw std::runtime_error("文字编码无效");
  q.replace("\r\n", "\n");
  q.replace('\r', '\n');
  auto scalars = q.toUcs4();
  if (scalars.empty() || scalars.size() > 256)
    throw std::runtime_error("每次发送 1–256 个字符");
  for (auto c : scalars)
    if ((c >= 0xd800 && c <= 0xdfff) || (c < 32 && c != 9 && c != 10) || (c >= 127 && c <= 159))
      throw std::runtime_error("文字中包含不支持的控制字符");
  push({{"type", "release"}});
  pending = ++text_id;
  text_deadline = now + 5s;
  text_status = "正在发送，等待远端确认…";
  push({{"type", "text"},
        {"layout_version", layout.at("layout_version")},
        {"text_id", pending},
        {"text", q.toUtf8().toStdString()}});
}

void ControlProtocol::tick(sdk::ControlClock::time_point now) {
  if (pending && now >= text_deadline)
    throw std::runtime_error("文字发送结果不确定，请检查远端内容，不要直接重发");
}

void ControlProtocol::finish() {
  queue.clear();
  move.reset();
  push({{"type", "stop"}});
  if (pending)
    text_status = "文字发送结果不确定，请检查远端内容，不要直接重发";
  pending = 0;
}

QImage convert_frame(const sdk::VideoFrame& f) {
  if (f.width < 1 || f.height < 1 || f.width > 8192 || f.height > 8192)
    throw std::runtime_error("视频尺寸无效");
  auto valid = [](const auto& p, int stride, int w, int h) {
    return stride >= w && p.size() >= size_t(stride) * h;
  };
  if (!valid(f.y, f.stride_y, f.width, f.height) ||
      !valid(f.u, f.stride_u, (f.width + 1) / 2, (f.height + 1) / 2) ||
      !valid(f.v, f.stride_v, (f.width + 1) / 2, (f.height + 1) / 2))
    throw std::runtime_error("视频数据无效");
  QImage image(f.width, f.height, QImage::Format_RGB32);
  if (image.isNull())
    throw std::runtime_error("无法分配画面缓冲");
  auto clip = [](int n) { return std::clamp(n, 0, 255); };
  for (int y = 0; y < f.height; ++y) {
    auto row = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 0; x < f.width; ++x) {
      int a = std::max(0, int(f.y[y * f.stride_y + x]) - 16),
          u = int(f.u[y / 2 * f.stride_u + x / 2]) - 128,
          v = int(f.v[y / 2 * f.stride_v + x / 2]) - 128;
      row[x] =
          qRgb(clip((298 * a + 409 * v + 128) >> 8), clip((298 * a - 100 * u - 208 * v + 128) >> 8),
               clip((298 * a + 516 * u + 128) >> 8));
    }
  }
  return image;
}

Controller::~Controller() {
  stop();
  if (worker_.joinable())
    worker_.join();
}

ControllerSnapshot Controller::snapshot() const {
  std::lock_guard lock(mutex_);
  auto s = snapshot_;
  s.text_pending = protocol_.pending != 0;
  s.text_accepted = protocol_.accepted;
  s.text_status = protocol_.text_status;
  if (!protocol_.layout.is_null()) {
    s.layout = protocol_.layout.at("layout_version");
    s.width = protocol_.layout.at("width");
    s.height = protocol_.layout.at("height");
    s.text_supported = protocol_.layout.value("text_input", false);
  }
  if (stop_) {
    s.layout = 0;
    s.text_supported = false;
  }
  return s;
}

void Controller::start(std::string url, std::string code, std::string name) {
  if (!snapshot().finished)
    return;
  if (worker_.joinable())
    worker_.join();
  while (!url.empty() && url.back() == '/')
    url.pop_back();
  stop_ = false;
  {
    std::lock_guard lock(mutex_);
    snapshot_ = {};
    snapshot_.finished = false;
    snapshot_.state = "requesting";
    protocol_ = {};
  }
  worker_ = std::thread([this, url, code, name] { run(url, code, name); });
}

void Controller::stop() {
  stop_ = true;
}

void Controller::fail(std::string error) {
  std::lock_guard lock(mutex_);
  if (snapshot_.error.empty())
    snapshot_.error = std::move(error);
  stop_ = true;
}

void Controller::input(sdk::Json event) {
  try {
    std::lock_guard lock(mutex_);
    if (stop_ || snapshot_.finished || protocol_.layout.is_null() || protocol_.pending)
      return;
    event["layout_version"] = protocol_.layout.at("layout_version");
    protocol_.push(std::move(event));
  } catch (const std::exception& e) {
    fail(e.what());
  }
}

void Controller::release() {
  try {
    std::lock_guard lock(mutex_);
    if (stop_ || snapshot_.finished || protocol_.layout.is_null())
      return;
    protocol_.move.reset();
    protocol_.push({{"type", "release"}});
  } catch (const std::exception& e) {
    fail(e.what());
  }
}

void Controller::send_text(std::string text) {
  std::lock_guard lock(mutex_);
  if (protocol_.queue.size() >= 254) {
    snapshot_.error = "控制消息积压，连接已停止";
    stop_ = true;
    throw std::runtime_error(snapshot_.error);
  }
  if (stop_ || snapshot_.finished)
    throw std::runtime_error("连接已结束");
  protocol_.text(std::move(text));
}

void Controller::run(std::string url, std::string code, std::string name) {
  std::string id, credential;
  try {
    sdk::Json body = {{"code", code}, {"name", name}};
    auto paired = request(url, "/api/v1/av/remote-control/redeem", "", &body);
    id = paired.at("resource_id");
    credential = paired.at("credential");
    auto empty = sdk::Json::object();
    auto grant = grant_view(request(url, "/api/v1/av/remote-control/sessions/" + id + "/request",
                                    "", &empty, {"", credential}));
    {
      std::lock_guard lock(mutex_);
      snapshot_.state = "pending";
      protocol_.grant = grant;
    }
    bool started = false;
    auto next = sdk::ControlClock::now();
    auto presence = next;
    while (!stop_) {
      if (sdk::ControlClock::now() >= presence) {
        request(url, "/api/v1/av/remote-control/sessions/" + id + "/heartbeat", "", &empty,
                {"", credential});
        presence = sdk::ControlClock::now() + 3s;
      }
      if (sdk::ControlClock::now() >= next) {
        auto snapshot =
            request(url, "/api/v1/av/remote-control/sessions/" + id, "", nullptr, {"", credential});
        grant = grant_view(snapshot.value("control_grant", sdk::Json::object()));
        if (stop_)
          break;
        auto state = grant.at("state").get<std::string>();
        if (state == "approved" && !started) {
          started = true;
          {
            std::lock_guard lock(mutex_);
            snapshot_.state = "connecting";
          }
          rtc_ = std::thread([this, url, grant, credential] { media(url, grant, credential); });
        } else if (state != "pending" && state != "approved")
          throw std::runtime_error("申请已结束：" + state);
        next = sdk::ControlClock::now() + 500ms;
      }
      std::this_thread::sleep_for(20ms);
    }
  } catch (const std::exception& e) {
    fail(e.what());
  }
  stop_ = true;

  if (rtc_.joinable())
    rtc_.join();
  if (!id.empty())
    try {
      sdk::Json empty = sdk::Json::object();
      request(url, "/api/v1/av/remote-control/sessions/" + id + "/stop", "", &empty,
              {"", credential});
    } catch (const std::exception& e) {
      fail(e.what());
    }
  {
    std::lock_guard lock(mutex_);
    protocol_.finish();
    protocol_.layout = nullptr;
    snapshot_.image = {};
    snapshot_.state = "ended";
    snapshot_.finished = true;
  }
}

void Controller::media(std::string url, sdk::Json grant, std::string credential) {
  std::unique_ptr<RtcConnection> connection;
  try {
    const auto run_media = [&]() -> sdk::Result<void> {
      sdk::Json empty = sdk::Json::object();
      auto access = av::request_result(url,
                                       "/api/v1/av/remote-control/sessions/" +
                                           grant.value("resource_id", std::string{}) + "/connect",
                                       "", &empty, {"", credential});
      if (!access) {
        return sdk::Result<void>::failure(access.failure_record());
      }
      auto credentials = av::media_credentials(url, access->value("grant_id", std::string{}), "",
                                               {"", credential}, &stop_);
      if (!credentials) {
        return sdk::Result<void>::failure(credentials.failure_record());
      }
      auto connect = *credentials;
      if (stop_)
        return sdk::Result<void>::success();
      connection =
          std::make_unique<RtcConnection>(connect.at("media_url"), connect.at("media_token"),
                                          grant.at("host_identity"), true, stop_);
      auto ready = connection->status();
      if (!ready) {
        return ready;
      }
      auto next = sdk::ControlClock::now(), move_at = next, layout_deadline = next + 10s;

      while (!stop_) {
        auto polled = connection->poll();
        if (!polled) {
          return polled;
        }
        auto received = connection->take_data();
        if (!received) {
          return sdk::Result<void>::failure(received.failure_record());
        }
        for (const auto& packet : *received) {
          std::lock_guard lock(mutex_);
          if (!stop_ && protocol_.receive(packet.bytes, packet.source))
            snapshot_.state = "connected";
        }
        const auto now = sdk::ControlClock::now();
        std::optional<sdk::Json> packet;
        {
          std::lock_guard lock(mutex_);
          protocol_.tick(now);
          if (now >= next) {
            protocol_.push({{"type", "heartbeat"}});
            next = now + 1s;
          }
          if (now >= move_at) {
            protocol_.flush_move();
            move_at = now + 34ms;
          }
          if (!protocol_.queue.empty())
            packet = protocol_.next();
          if (protocol_.layout.is_null() && now >= layout_deadline)
            return sdk::Result<void>::failure(sdk::make_failure(
                sdk::ErrorDomain::data, sdk::ErrorCode::timeout, "未收到被控端布局确认"));
        }
        if (packet) {
          auto raw = packet->dump();
          auto sent = connection->send({raw.begin(), raw.end()});
          if (!sent) {
            return sent;
          }
        }
        if (auto frame = connection->take_frame()) {
          auto image = convert_frame(*frame);
          std::lock_guard lock(mutex_);
          if (!stop_) {
            snapshot_.image = std::move(image);
            ++snapshot_.frames;
          }
        }
        std::this_thread::sleep_for(5ms);
      }
      // Best-effort explicit stop; cancellation/watchdog remain authoritative.
      if (connection->connected()) {
        sdk::Json event;
        {
          std::lock_guard lock(mutex_);
          protocol_.finish();
          event = protocol_.next();
        }
        auto raw = event.dump();
        (void)connection->send({raw.begin(), raw.end()});
      }
      return sdk::Result<void>::success();
    };
    auto outcome = run_media();
    if (!outcome && !stop_ && outcome.error().code != sdk::ErrorCode::session_closed) {
      fail(outcome.failure_record()->message);
    }
  } catch (const std::exception& e) {
    if (!stop_)
      fail(e.what());
  }
  if (connection) {
    connection->close();
  }
  stop_ = true;
}
}  // namespace remote_app
