// Full control-plane + native media integration. All media is synthetic; no
// device access.
#include "../session.hpp"
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QUuid>
#include <atomic>
#include <iostream>
using namespace std::chrono_literals;
namespace sdk = livekit::sdk;
static void check(bool ok, const char *msg) {
  if (!ok)
    throw std::runtime_error(msg);
}
template <class F> void until(F f, const char *msg) {
  auto end = std::chrono::steady_clock::now() + 25s;
  while (!f()) {
    check(std::chrono::steady_clock::now() < end, msg);
    QCoreApplication::processEvents();
    std::this_thread::sleep_for(30ms);
  }
}
struct Client {
  explicit Client(QString url) : base(std::move(url)), network(nullptr) {}
  QString base, token, guest;
  QNetworkAccessManager network;
  QJsonObject call(QString path, QJsonObject body = {}, int expected = 200,
                   bool get = false) {
    QNetworkRequest req(QUrl(base + "/api/v1/" + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("X-AV-Client", "desktop");
    req.setRawHeader("Idempotency-Key", QUuid::createUuid().toByteArray());
    if (!token.isEmpty())
      req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
    if (!guest.isEmpty())
      req.setRawHeader("X-AV-Guest", guest.toUtf8());
    req.setTransferTimeout(10000);
    auto *reply = get ? network.get(req)
                      : network.post(req, QJsonDocument(body).toJson());
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto data = reply->readAll();
    reply->deleteLater();
    if (status != expected)
      throw std::runtime_error(
          (path + " HTTP " + QString::number(status) + " " + data)
              .toStdString());
    return QJsonDocument::fromJson(data).object();
  }
};
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    QString base = qEnvironmentVariable("CONFERENCE_TEST_URL");
    check(!base.isEmpty(), "Set CONFERENCE_TEST_URL");
    Client owner{base}, guest{base};
    owner.token =
        owner
            .call("auth/register", {{"name", "meeting_owner"},
                                    {"password", "test-passphrase-12345"}})
            .value("session_token")
            .toString();
    guest.guest =
        guest.call("conference/guest").value("guest_token").toString();
    auto meeting =
        owner.call("conference/meetings", {{"title", "真实媒体联调"}});
    auto number = meeting.value("number").toString(),
         id = meeting.value("id").toString(),
         prefix = "conference/meetings/" + id + "/";
    owner.call("conference/join", {{"number", number}, {"name", "主持人"}});
    auto g =
        guest.call("conference/join", {{"number", number}, {"name", "游客"}});
    auto gid = g.value("member_id").toString();
    guest.call(prefix + "credentials", {}, 403);
    owner.call(prefix + "approve", {{"target", gid}});
    auto hcred = owner.call(prefix + "credentials"),
         gcred = guest.call(prefix + "credentials");
    auto options = [](QJsonObject c) {
      sdk::RoomOptions o;
      o.url = c.value("media_url").toString().toStdString();
      o.token = c.value("media_token").toString().toStdString();
      o.room = c.value("room").toString().toStdString();
      o.audio_playout = false;
      o.microphone = false;
      return o;
    };
    std::atomic<int> frames{0}, audio{0}, messages{0};
    sdk::RoomCallback callbacks;
    callbacks.on_data_received = [&](const auto &data, const auto &params) {
      if (params.topic == demo::chat_topic && !data.empty())
        messages++;
    };
    sdk::Room host(callbacks), visitor;
    auto ho = options(hcred);
    ho.video_frame_handler = [&](const auto &) { ++frames; };
    ho.audio_frame_stream = std::make_shared<sdk::AudioFrameStream>();
    host.join_with_token(ho.url, ho.token, ho);
    auto go = options(gcred);
    visitor.join_with_token(go.url, go.token, go);
    sdk::MediaSpec video;
    video.demo = true;
    video.width = 160;
    video.height = 90;
    video.fps = 10;
    auto camera =
        visitor.local_participant()
            .publish_track(video, {"合成摄像头", "", sdk::TrackSource::camera})
            .get();
    sdk::MediaSpec tone;
    tone.demo = true;
    tone.video = false;
    auto sound =
        visitor.local_participant()
            .publish_track(tone, {"合成音频", "", sdk::TrackSource::microphone})
            .get();
    until([&] { return frames >= 5; }, "No actual video decoded");
    until(
        [&] {
          auto r = ho.audio_frame_stream->read(10ms);
          if (r.frame)
            for (auto sample : r.frame->samples)
              if (std::abs(int(sample)) > 100)
                return true;
          return false;
        },
        "No non-silent audio decoded");
    visitor.local_participant()
        .publish_data(demo::encode_chat("integration-1", "你好，中文会议消息"),
                      {true, demo::chat_topic})
        .get();
    until([&] { return messages > 0; }, "No reliable chat");
    owner.call(prefix + "mute", {{"target", gid}});
    until(
        [&] {
          for (auto &t : visitor.local_participant().info().tracks)
            if (t.sid == sound.sid())
              return t.muted;
          return false;
        },
        "Host mute not applied");
    sound.set_muted(false).get();
    guest.call(prefix + "share", {{"enabled", true}});
    until(
        [&] {
          auto state = guest.call(prefix + "presence");
          for (auto v : state.value("operations").toArray()) {
            auto op = v.toObject();
            if (op.value("kind") == "share-on" && op.value("state") == "done")
              return true;
          }
          return false;
        },
        "Share authorization not applied");
    auto screen = visitor.local_participant()
                      .publish_track(video, {"合成共享", "",
                                             sdk::TrackSource::screen_share})
                      .get();
    int before = frames;
    until([&] { return frames > before + 5; }, "Sharing interrupted video");
    guest.call(prefix + "share", {{"enabled", false}});
    until(
        [&] {
          for (auto &t : visitor.local_participant().info().tracks)
            if (t.sid == screen.sid())
              return false;
          return true;
        },
        "Screen permission not revoked");
    visitor.disconnect();
    visitor.wait();
    gcred = guest.call(prefix + "credentials");
    go = options(gcred);
    visitor.join_with_token(go.url, go.token, go);
    check(visitor.connection_state() == sdk::ConnectionState::connected,
          "Rejoin failed");
    owner.call(prefix + "remove", {{"target", gid}});
    until(
        [&] {
          return visitor.connection_state() ==
                 sdk::ConnectionState::disconnected;
        },
        "Removed member remains connected");
    visitor.wait();
    bool rejected = false;
    try {
      sdk::Room old;
      old.join_with_token(go.url, go.token, go);
    } catch (...) {
      rejected = true;
    }
    check(rejected, "Revoked token rejoined media server");
    owner.call(prefix + "end");
    until(
        [&] {
          return host.connection_state() == sdk::ConnectionState::disconnected;
        },
        "Ended meeting remains connected");
    host.wait();
    rejected = false;
    try {
      sdk::Room old;
      old.join_with_token(ho.url, ho.token, options(hcred));
    } catch (...) {
      rejected = true;
    }
    check(rejected, "Ended meeting accepts old token");
    std::cout << "{\"passed\":true,\"video_decoded\":true,\"audio_decoded\":"
                 "true,\"chat\":true,\"host_mute\":true,\"share_permissions\":"
                 "true,\"rejoin\":true,\"revoked_token_denied\":true,\"end_"
                 "cleanup\":true,\"native_devices\":false}\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
