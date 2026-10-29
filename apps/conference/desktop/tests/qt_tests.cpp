#include "../qt/window.hpp"
#include <QDir>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStackedWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
class MeetingTests : public QObject {
  Q_OBJECT
private slots:
  void pixels() {
    demo::sdk::VideoFrame f;
    f.width = 321;
    f.height = 181;
    f.stride_y = 328;
    f.stride_u = 164;
    f.stride_v = 164;
    f.y.assign(328 * 181, 81);
    f.u.assign(164 * 91, 90);
    f.v.assign(164 * 91, 240);
    auto image = meeting::convert_frame(f);
    QCOMPARE(image.size(), QSize(321, 181));
    auto pixel = image.pixelColor(160, 90);
    QVERIFY(pixel.red() > 245);
    QVERIFY(pixel.green() < 10);
    QVERIFY(pixel.blue() < 10);
    QCOMPARE(meeting::fit_video({0, 0, 400, 400}, {1600, 900}).size(),
             QSize(400, 225));
    f.u.clear();
    QVERIFY_EXCEPTION_THROWN(meeting::convert_frame(f), std::invalid_argument);
  }
  void boundedFrames() {
    meeting::FrameConverter c;
    int heartbeats = 0;
    QTimer responsive;
    connect(&responsive, &QTimer::timeout, [&] { ++heartbeats; });
    responsive.start(1);
    for (int n = 0; n < 100; ++n) {
      meeting::FrameConverter::Frames batch;
      for (int i = 0; i < 9; i++) {
        auto f = std::make_shared<demo::sdk::VideoFrame>();
        f->width = 640;
        f->height = 360;
        f->stride_y = 640;
        f->stride_u = f->stride_v = 320;
        f->y.assign(640 * 360, 81);
        f->u.assign(320 * 180, 90);
        f->v.assign(320 * 180, 240);
        batch[std::to_string(i)] = f;
      }
      c.submit(std::move(batch), {640, 360});
    }
    QTRY_COMPARE_WITH_TIMEOUT(c.images().size(), size_t(9), 3000);
    QTest::qWait(20);
    QVERIFY(heartbeats > 0);
    c.clear();
    QTest::qWait(30);
    QVERIFY(c.images().empty());
  }
  void pagesAndScreenshots() {
    meeting::Window window(false);
    window.resize(1180, 780);
    window.show();
    QTest::qWait(20);
    auto *pages = window.findChild<QStackedWidget *>();
    QCOMPARE(pages->currentWidget()->objectName(), QString("homePage"));
    auto save = [&](QString name) {
      auto dir = qEnvironmentVariable("AV_MEETING_SCREENSHOT_DIR");
      if (dir.isEmpty())
        return;
      QDir().mkpath(dir);
      for (int scale : {1, 2}) {
        QImage image(window.size() * scale,
                     QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(scale);
        image.fill(Qt::transparent);
        window.render(&image);
        QVERIFY(image.save(dir + "/" + name + QString("-%1x.png").arg(scale)));
      }
    };
    save("home");
    QTest::mouseClick(window.findChild<QPushButton *>("joinButton"),
                      Qt::LeftButton);
    QTest::qWait(10);
    auto *number = window.findChild<QLineEdit *>("meetingNumber");
    QVERIFY(number);
    number->setText("123");
    auto *dialog = number->window();
    QTest::mouseClick(dialog->findChild<QPushButton *>("primary"),
                      Qt::LeftButton);
    QCOMPARE(pages->currentWidget()->objectName(), QString("homePage"));
    dialog->close();
    QJsonArray members;
    for (int i = 0; i < 25; i++)
      members.append(QJsonObject{{"id", QString::number(i)},
                                 {"name", QString("参会者 %1").arg(i)},
                                 {"status", "approved"},
                                 {"online", true},
                                 {"registered", true}});
    QJsonObject state{{"id", "demo"},
                      {"title", "产品设计评审 · 中文与长标题布局验证"},
                      {"number", "123456789"},
                      {"state", "active"},
                      {"status", "waiting"},
                      {"self", "0"},
                      {"host", "0"},
                      {"members", members}};
    window.applyBusiness(state);
    QCOMPARE(pages->currentWidget()->objectName(), QString("waitingPage"));
    save("waiting");
    state["status"] = "approved";
    window.applyBusiness(state);
    demo::Snapshot snapshot;
    snapshot.state = demo::State::Connected;
    snapshot.finished = false;
    for (int i = 0; i < 25; i++) {
      demo::Participant p;
      p.sid = "p" + std::to_string(i);
      p.identity = p.sid;
      p.name = "参会者 " + std::to_string(i);
      snapshot.participants[p.sid] = p;
    }
    window.applyMedia(snapshot);
    QCOMPARE(pages->currentWidget()->objectName(), QString("meetingPage"));
    save("meeting");
    QTest::mouseClick(window.findChild<QPushButton *>("nextPage"),
                      Qt::LeftButton);
    window.resize(800, 560);
    save("meeting-narrow");
    // A visible tile must follow layout mode and disappear with its member.
    auto &speaker = snapshot.participants.at("p24");
    demo::TrackInfo track;
    track.sid = "screen24";
    track.name = "共享屏幕";
    track.kind = demo::rtc::PublicationKind::video;
    track.source = demo::rtc::PublicationSource::screen;
    track.subscribed = true;
    speaker.tracks.push_back(track);
    window.applyMedia(snapshot);
    window.findChild<QComboBox *>("layoutMode")->setCurrentIndex(2);
    auto visibleTiles = [&] {
      QList<QWidget *> result;
      for (auto *tile : window.findChildren<QWidget *>("videoTile"))
        if (tile->isVisible()) result.append(tile);
      return result;
    };
    QCOMPARE(visibleTiles().size(), 1);
    QVERIFY(visibleTiles().front()->accessibleName().contains("共享屏幕"));
    snapshot.participants.erase("p24");
    window.applyMedia(snapshot);
    QVERIFY(!visibleTiles().front()->accessibleName().contains("共享屏幕"));
    window.findChild<QComboBox *>("layoutMode")->setCurrentIndex(0);
    QCOMPARE(visibleTiles().size(), 9);
    snapshot.error = "网络连接已中断，请检查网络后重新加入";
    snapshot.state = demo::State::Error;
    window.applyMedia(snapshot);
    QVERIFY(!window.findChild<QPushButton *>("microphoneButton")->isEnabled());
    save("meeting-error");
    state["state"] = "ended";
    window.applyBusiness(state);
    QCOMPARE(pages->currentWidget()->objectName(), QString("homePage"));
    window.close();
  }
  void cancellationDoesNotBlockGui() {
    // A stalled signal endpoint exercises the actual media supervisor, without
    // opening audio/video devices or running the desktop event loop manually.
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    demo::Session session;
    demo::SessionOptions options;
    options.connection.url = QString("ws://127.0.0.1:%1").arg(server.serverPort()).toStdString();
    options.connection.token = "cancel-test";
    options.audio_playout = false;
    options.microphone = false;
    session.start(options);
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections() || session.snapshot().finished, 5000);
    QElapsedTimer elapsed;
    elapsed.start();
    session.stop();
    QVERIFY(elapsed.elapsed() < 100);
    QTRY_VERIFY_WITH_TIMEOUT(session.snapshot().finished, 10000);
  }
  void loginAndQuickMeetingFlow() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settings.path());
    QSettings prefs;
    prefs.setValue("server",
                   QString("http://127.0.0.1:%1").arg(server.serverPort()));
    connect(&server, &QTcpServer::newConnection, &server, [&] {
      while (server.hasPendingConnections()) {
        auto *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [socket] {
          auto bytes =
              socket->property("request").toByteArray() + socket->readAll();
          socket->setProperty("request", bytes);
          int end = bytes.indexOf("\r\n\r\n");
          if (end < 0)
            return;
          auto headers = QString::fromLatin1(bytes.left(end));
          auto match =
              QRegularExpression("Content-Length: ([0-9]+)",
                                 QRegularExpression::CaseInsensitiveOption)
                  .match(headers);
          int length = match.hasMatch() ? match.captured(1).toInt() : 0;
          if (bytes.size() < end + 4 + length)
            return;
          QByteArray body = "{}";
          auto route = bytes.split(' ').value(1);
          if (route == "/api/v1/auth/login")
            body =
                R"({"session_token":"test-session","user":{"id":"u1","name":"alice"}})";
          else if (route == "/api/v1/conference/meetings" &&
                   bytes.startsWith("GET"))
            body = "[]";
          else if (route == "/api/v1/conference/meetings")
            body = R"({"id":"m1","number":"123456789"})";
          else if (route == "/api/v1/conference/join")
            body = R"({"id":"m1","member_id":"p1"})";
          else if (route.endsWith("/presence"))
            body =
                R"({"id":"m1","number":"123456789","title":"测试会议","state":"active","status":"approved","self":"p1","host":"p1","members":[],"operations":[],"version":1})";
          socket->write("HTTP/1.1 200 OK\r\nContent-Type: "
                        "application/json\r\nContent-Length: " +
                        QByteArray::number(body.size()) +
                        "\r\nConnection: close\r\n\r\n" + body);
          socket->disconnectFromHost();
        });
        connect(socket, &QTcpSocket::disconnected, socket,
                &QObject::deleteLater);
      }
    });
    meeting::Window window(false);
    window.show();
    QTest::mouseClick(window.findChild<QPushButton *>("accountButton"),
                      Qt::LeftButton);
    auto *name = window.findChild<QLineEdit *>("loginName");
    QVERIFY(name);
    name->setText("alice");
    window.findChild<QLineEdit *>("loginPassword")->setText("test-passphrase");
    QTest::mouseClick(name->window()->findChild<QPushButton *>("primary"),
                      Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(
        window.findChild<QLineEdit *>("loginName") == nullptr, 5000);
    QTest::mouseClick(window.findChild<QPushButton *>("quickButton"),
                      Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(
        window.findChild<QStackedWidget *>()->currentWidget()->objectName(),
        QString("meetingPage"), 5000);
    window.close();
  }
  void staleNetworkResponse() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    meeting::Api api;
    api.base = QString("http://127.0.0.1:%1").arg(server.serverPort());
    bool called = false;
    api.request("/slow", {}, [&](QJsonValue, QString) { called = true; }, true);
    QTRY_VERIFY(server.hasPendingConnections());
    auto *socket = server.nextPendingConnection();
    api.cancel();
    QTest::qWait(50);
    QVERIFY(!called);
    socket->deleteLater();
  }
};
QTEST_MAIN(MeetingTests)
#include "qt_tests.moc"
