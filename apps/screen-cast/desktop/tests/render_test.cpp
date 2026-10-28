#include "features/ui.hpp"
#include "services/controller.hpp"
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace screen_cast;
namespace {
std::vector<livekit::sdk::ScreenCaptureDevice> displays() {
  return {{"main", "内建显示器", 0, 0, 2560, 1440, true},
          {"portrait", "外接显示器", -1080, 0, 1080, 1920, false}};
}
std::shared_ptr<livekit::sdk::VideoFrame> frame() {
  auto f = std::make_shared<livekit::sdk::VideoFrame>();
  f->width = 320;
  f->height = 180;
  f->stride_y = 336;
  f->stride_u = f->stride_v = 168;
  f->y.assign(336 * 180, 110);
  f->u.assign(168 * 90, 170);
  f->v.assign(168 * 90, 80);
  return f;
}
class FakeBackend : public Backend {
public:
  Snapshot state;
  int starts = 0, stops = 0;
  std::vector<livekit::sdk::ScreenCaptureDevice> devices() override {
    return displays();
  }
  void start(std::string, std::string,
             livekit::sdk::ScreenCaptureDevice) override {
    ++starts;
    state.finished = false;
    state.state = "connecting";
  }
  void stop() override {
    ++stops;
    state.state = "stopping";
  }
  Snapshot snapshot() const override { return state; }
};
} // namespace
class UiTest : public QObject {
  Q_OBJECT
private slots:
  void validation() {
    QVERIFY(validCode("0123456789"));
    QVERIFY(!validCode("123456789"));
    QVERIFY(!validCode("１２３４５６７８９０"));
    QCOMPARE(normalizedCode(" 01234 56789\n"), QString("0123456789"));
    QVERIFY(validServer("https://192.168.1.10:8080"));
    QVERIFY(!validServer("file:///tmp"));
    QVERIFY(!validServer("https://user:pass@host"));
    QVERIFY(!validServer("http://"));
    QVERIFY(!validServer("http://host?q=1"));
  }
  void controls() {
    Window w;
    w.setDevices({});
    auto *start = w.findChild<QPushButton *>("start");
    QVERIFY(!start->isEnabled());
    w.setDevices(displays());
    w.findChild<QLineEdit *>("code")->insert("01234 56789");
    w.findChild<QLineEdit *>("code")->insert("x");
    QCOMPARE(w.code(), QString("0123456789"));
    QVERIFY(start->isEnabled());
    QSignalSpy spy(&w, &Window::startRequested);
    start->click();
    start->click();
    QCOMPARE(spy.count(), 1);
    QVERIFY(!w.findChild<QLineEdit *>("server")->isEnabled());
    Snapshot s;
    s.finished = false;
    s.state = "paired";
    w.present(s);
    QVERIFY(w.findChild<QPushButton *>("stop")->isEnabled());
    s.state = "stopping";
    w.present(s);
    QVERIFY(!w.findChild<QPushButton *>("stop")->isEnabled());
    s.finished = true;
    s.error = "Screen recording permission denied";
    w.present(s);
    QVERIFY(w.findChild<QLabel *>("error")->text().contains("权限"));
    QVERIFY(start->isEnabled());
  }
  void conversion() {
    auto f = frame();
    auto image = convertFrame(*f, {160, 90});
    QCOMPARE(image.size(), QSize(160, 90));
    QCOMPARE(image.pixelColor(0, 0), QColor(33, 132, 194));
    f->u.clear();
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             convertFrame(*f, {160, 90}));
  }
  void staleFrames() {
    FrameConverter converter;
    converter.submit(frame());
    QTRY_VERIFY(!converter.image().isNull());
    converter.clear();
    QVERIFY(converter.image().isNull());
    for (int i = 0; i < 20; ++i) {
      converter.submit(frame());
      converter.clear();
    }
    QTest::qWait(100);
    QVERIFY(converter.image().isNull());
  }
  void lifecycle() {
    Window w;
    auto b = std::make_unique<FakeBackend>();
    auto *backend = b.get();
    Controller c(w, std::move(b));
    w.show();
    QTRY_COMPARE(w.findChild<QListWidget *>("displays")->count(), 2);
    w.findChild<QLineEdit *>("code")->setText("0123456789");
    w.findChild<QPushButton *>("start")->click();
    QCOMPARE(backend->starts, 1);
    w.findChild<QPushButton *>("stop")->click();
    QCOMPARE(backend->stops, 1);
    QTRY_COMPARE(w.findChild<QLabel *>("status")->text(),
                 QString("正在停止投屏…"));
    w.close();
    QVERIFY(w.isVisible());
    backend->state.finished = true;
    backend->state.state = "ended";
    QTRY_VERIFY(!w.isVisible());
  }
  void screenshots() {
    Window w;
    w.setDevices(displays());
    w.findChild<QLineEdit *>("code")->setText("0123456789");
    w.show();
    QDir().mkpath("screenshots");
    const QString suffix = qEnvironmentVariable("SC_SCREENSHOT_SUFFIX");
    for (auto size : {QSize(1040, 720), QSize(800, 600)}) {
      w.resize(size);
      QTest::qWait(30);
      const auto prefix =
          "screenshots/desktop-" + QString::number(size.width()) + suffix;
      for (auto state :
           {"idle", "connecting", "paired", "streaming", "stopping", "ended"}) {
        Snapshot s;
        s.state = state;
        s.finished = s.state == "idle" || s.state == "ended";
        if (s.state == "ended")
          s.error = "Screen recording permission denied";
        w.present(s);
        if (s.state == "streaming")
          w.setPreview(convertFrame(*frame(), {320, 180}));
        QTest::qWait(10);
        const auto *list = w.findChild<QListWidget *>("displays");
        const auto *action =
            w.findChild<QPushButton *>(s.finished ? "start" : "stop");
        QVERIFY(!list->geometry().intersects(action->geometry()));
        QVERIFY(list->parentWidget()->rect().contains(list->geometry()));
        QVERIFY(w.grab().save(prefix + "-" + state + ".png"));
      }
      w.findChild<QPushButton *>("nav1")->click();
      QTest::qWait(10);
      QVERIFY(w.grab().save(prefix + "-settings.png"));
      w.findChild<QPushButton *>("nav0")->click();
    }
  }
};
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QTemporaryDir settings;
  app.setOrganizationName("AVPlatformTest");
  app.setApplicationName("ScreenCastTest");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                     settings.path());
  UiTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "render_test.moc"
