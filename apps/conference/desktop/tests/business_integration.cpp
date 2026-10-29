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
#include <chrono>
#include <iostream>
using namespace std::chrono_literals;
namespace sdk = livekit::sdk;

static void check(bool ok, const char* msg) {
  if (!ok)
    throw std::runtime_error(msg);
}

template <class F>
void until(F f, const char* msg) {
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

  QJsonObject call(QString path, QJsonObject body = {}, int expected = 200, bool get = false) {
    QNetworkRequest req(QUrl(base + "/api/v1/" + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("X-AV-Client", "desktop");
    req.setRawHeader("Idempotency-Key", QUuid::createUuid().toByteArray());
    if (!token.isEmpty())
      req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
    if (!guest.isEmpty())
      req.setRawHeader("X-AV-Guest", guest.toUtf8());
    req.setTransferTimeout(10000);
    auto* reply = get ? network.get(req) : network.post(req, QJsonDocument(body).toJson());
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto data = reply->readAll();
    reply->deleteLater();
    if (status != expected)
      throw std::runtime_error(
          (path + " HTTP " + QString::number(status) + " " + data).toStdString());
    return QJsonDocument::fromJson(data).object();
  }
};

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  try {
    QString base = qEnvironmentVariable("CONFERENCE_TEST_URL");
    check(!base.isEmpty(), "Set CONFERENCE_TEST_URL");
    Client owner{base}, guest{base};
    owner.token = owner
                      .call("auth/register",
                            {{"name", "meeting_owner"}, {"password", "test-passphrase-12345"}})
                      .value("session_token")
                      .toString();
    guest.guest = guest.call("conference/guest").value("guest_token").toString();
    auto meeting = owner.call("conference/meetings", {{"title", "真实媒体联调"}});
    auto number = meeting.value("number").toString(), id = meeting.value("id").toString(),
         prefix = "conference/meetings/" + id + "/";
    owner.call("conference/join", {{"number", number}, {"name", "主持人"}});
    auto g = guest.call("conference/join", {{"number", number}, {"name", "游客"}});
    auto gid = g.value("member_id").toString();
    guest.call(prefix + "credentials", {}, 403);
    owner.call(prefix + "approve", {{"target", gid}});
    auto hcred = owner.call(prefix + "credentials"), gcred = guest.call(prefix + "credentials");
    auto options = [](QJsonObject c) {
      demo::SessionOptions o;
      o.connection.url = c.value("media_url").toString().toStdString();
      o.connection.token = c.value("media_token").toString().toStdString();
      o.room = c.value("room").toString().toStdString();
      o.audio_playout = false;
      o.microphone = false;
      return o;
    };
    demo::Session host, visitor;
    auto ho = options(hcred);
    host.start(ho);
    auto go = options(gcred);
    sdk::MediaSpec video;
    video.demo = true;
    video.width = 160;
    video.height = 90;
    video.fps = 10;
    sdk::MediaSpec tone;
    tone.demo = true;
    tone.video = false;
    go.media = {video, tone};
    visitor.start(go);
    until([&] { return host.snapshot().received_frames >= 5; }, "No actual video decoded");
    until([&] { return host.snapshot().non_silent_audio_frames > 0; },
          "No non-silent audio decoded");
    auto local_tracks = [&] {
      const auto snapshot = visitor.snapshot();
      if (auto p = snapshot.participants.find(snapshot.local_sid); p != snapshot.participants.end())
        return p->second.tracks;
      return std::vector<demo::TrackInfo>{};
    };
    auto source_track = [&](demo::rtc::PublicationSource source) {
      for (const auto& track : local_tracks())
        if (track.source == source)
          return track.sid;
      return std::string{};
    };
    const auto sound = source_track(demo::rtc::PublicationSource::microphone);
    check(!sound.empty(), "Microphone-role publication missing");
    check(visitor.send_chat("你好，中文会议消息"), "Cannot enqueue reliable chat");
    until([&] { return !host.snapshot().chat.messages().empty(); }, "No reliable chat");
    owner.call(prefix + "mute", {{"target", gid}});
    until(
        [&] {
          for (auto& t : local_tracks())
            if (t.sid == sound)
              return t.muted;
          return false;
        },
        "Host mute not applied");
    check(visitor.set_muted(sound, false), "Unmute rejected");
    until([&] { return !visitor.snapshot().changing_video; }, "Unmute pending");
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
    check(visitor.add_video(video, demo::rtc::PublicationSource::screen), "Screen add rejected");
    until(
        [&] {
          return !visitor.snapshot().changing_video &&
                 !source_track(demo::rtc::PublicationSource::screen).empty();
        },
        "Screen publish missing");
    const auto screen = source_track(demo::rtc::PublicationSource::screen);
    const auto before = host.snapshot().received_frames;
    until([&] { return host.snapshot().received_frames > before + 5; },
          "Sharing interrupted video");
    guest.call(prefix + "share", {{"enabled", false}});
    until(
        [&] {
          for (auto& t : local_tracks())
            if (t.sid == screen)
              return false;
          return true;
        },
        "Screen permission not revoked");
    check(visitor.snapshot().state == demo::State::Connected && visitor.snapshot().error.empty(),
          "Screen revoke terminated the conference");
    const auto continuing = host.snapshot();
    until(
        [&] {
          const auto s = host.snapshot();
          return s.received_frames > continuing.received_frames + 5 &&
                 s.non_silent_audio_frames > continuing.non_silent_audio_frames + 5;
        },
        "Screen revoke stopped unrelated camera/audio graphs");
    visitor.stop();
    until([&] { return visitor.snapshot().finished; }, "Visitor stop");
    gcred = guest.call(prefix + "credentials");
    go = options(gcred);
    visitor.start(go);
    until([&] { return visitor.snapshot().state == demo::State::Connected; }, "Rejoin pending");
    check(visitor.snapshot().state == demo::State::Connected, "Rejoin failed");
    owner.call(prefix + "remove", {{"target", gid}});
    until([&] { return visitor.snapshot().finished; }, "Removed member remains connected");

    bool rejected = false;
    try {
      demo::Session old;
      old.start(go);
      until(
          [&] { return old.snapshot().finished || old.snapshot().state == demo::State::Connected; },
          "Old token pending");
      rejected = old.snapshot().state == demo::State::Error;
    } catch (...) {
      rejected = true;
    }
    check(rejected, "Revoked token rejoined media server");
    owner.call(prefix + "end");
    until([&] { return host.snapshot().finished; }, "Ended meeting remains connected");

    rejected = false;
    try {
      demo::Session old;
      old.start(options(hcred));
      until(
          [&] { return old.snapshot().finished || old.snapshot().state == demo::State::Connected; },
          "Ended token pending");
      rejected = old.snapshot().state == demo::State::Error;
    } catch (...) {
      rejected = true;
    }
    check(rejected, "Ended meeting accepts old token");
    std::cout << "{\"passed\":true,\"video_decoded\":true,\"audio_decoded\":"
                 "true,\"chat\":true,\"host_mute\":true,\"share_permissions\":"
                 "true,\"rejoin\":true,\"revoked_token_denied\":true,\"end_"
                 "cleanup\":true,\"native_devices\":false}\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
