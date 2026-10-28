#include "window.hpp"
#include <QtTest>
#include <QBuffer>
#include <cstring>
#include "utoolkit/logging/logger.h"
#include <QAction>
#include <QFile>
#include <QLabel>
#include <QListView>
#include <QMimeData>
#include <QSlider>
#include <QTemporaryDir>
#include <QUrl>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QDir>

using namespace player_demo;
namespace {
QString fixture(const QString& name) { return QString(PLAYER_FIXTURES) + "/" + name; }
struct AudioStats {
  int opens = 0, resets = 0, pauses = 0, resumes = 0, frames = 0;
  int pending = 0; bool paused = false, fail = false;
};
class FakeOutput final : public AudioOutput {
 public:
  explicit FakeOutput(std::shared_ptr<AudioStats> stats = std::make_shared<AudioStats>()) : stats(std::move(stats)) {}
  void open() override { ++stats->opens; stats->pending = 0; stats->paused = false; }
  bool canAccept() const override { return stats->fail || (!stats->paused && stats->pending < 6000); }
  void enqueue(const sdk::media::AudioFrame& frame) override { ++stats->frames; if (!stats->fail) stats->pending += frame.pcm->size(); }
  void pump() override { if (stats->fail) stats->pending = 0; else if (!stats->paused) stats->pending = std::max(0, stats->pending - 960); }
  void suspend(bool value) override { stats->paused = value; if (value) ++stats->pauses; else ++stats->resumes; }
  void reset() override { ++stats->resets; stats->pending = 0; stats->paused = false; }
  void setVolume(float) override {}
  bool drained() const override { return stats->pending == 0; }
  QString warning() const override { return stats->fail ? QStringLiteral("音频写入失败，将静音播放") : QString{}; }
  std::shared_ptr<AudioStats> stats;
};
QAction* action(PlayerWindow& window, const char* name) { return window.findChild<QAction*>(name); }
}
class PlayerTests : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() {
    utoolkit::logging::Logger::GetInstance().Initialize("player-test", "[%l] %v", utoolkit::logging::LogLevel::Warn);
  }
  void pcmPartialWrites() {
    class ShortWriter : public QIODevice {
     public:
      QByteArray bytes; bool fail = false;
      ShortWriter() { open(QIODevice::WriteOnly); }
      qint64 readData(char*, qint64) override { return -1; }
      qint64 writeData(const char* data, qint64 count) override {
        if (fail) return -1;
        count = std::min<qint64>(3, count); bytes.append(data, count); return count;
      }
    } device;
    sdk::media::AudioFrame frame; frame.pcm = std::make_shared<const std::vector<float>>(std::vector<float>{-2.f, -.5f, .5f, 2.f});
    PcmBuffer buffer; QVERIFY(buffer.load(frame, false)); QVERIFY(!buffer.load(frame, false));
    QCOMPARE(buffer.write(device, 0), 0);
    while (!buffer.empty()) QVERIFY(buffer.write(device, 7) > 0);
    QCOMPARE(device.bytes, QByteArray(reinterpret_cast<const char*>(frame.pcm->data()), 16));
    device.bytes.clear(); QVERIFY(buffer.load(frame, true));
    while (!buffer.empty()) QVERIFY(buffer.write(device, 5) > 0);
    QCOMPARE(device.bytes.size(), 8); qint16 values[4]; std::memcpy(values, device.bytes.constData(), 8);
    QCOMPARE(values[0], -32767); QCOMPARE(values[3], 32767);
    QVERIFY(buffer.load(frame, false)); device.fail = true; QCOMPARE(buffer.write(device, 9), -1); QVERIFY(!buffer.empty());
    buffer.clear(); QVERIFY(buffer.empty());
  }
  void playlist() {
    PlaylistModel model; model.append({"/one.mp4", "/two.mp4", "/three.mp4"});
    const auto id = model.item(1).id; model.setCurrent(id);
    QCOMPARE(model.nextRow(RepeatMode::Sequential), 2);
    QCOMPARE(model.nextRow(RepeatMode::One), 1);
    QVERIFY(model.moveRows({}, 1, 1, {}, 3)); QCOMPARE(model.rowForId(id), 2);
    QCOMPARE(model.nextRow(RepeatMode::Sequential), -1);
    QCOMPARE(model.nextRow(RepeatMode::All), 0);
    QCOMPARE(model.nextRow(RepeatMode::One, true), -1);
    std::unique_ptr<QMimeData> drag(model.mimeData({model.index(2)}));
    model.dropMimeData(drag.get(), Qt::MoveAction, 0, 0, {});
    QCOMPARE(model.rowForId(id), 0); QCOMPARE(model.rowCount(), 3);
    QSignalSpy dropped(&model, &PlaylistModel::filesDropped);
    QMimeData external; external.setUrls({QUrl::fromLocalFile("/new.mp4"), QUrl("https://example.org/a.mp4")});
    QVERIFY(model.dropMimeData(&external, Qt::CopyAction, -1, 0, {})); QCOMPARE(dropped.count(), 1);
    QCOMPARE(dropped.at(0).at(0).toStringList(), QStringList{"/new.mp4"});
    QVERIFY(model.removeRows(0, 1)); QVERIFY(model.currentId().isEmpty());
  }
  void settings() {
    QTemporaryDir directory; const auto file = directory.filePath("state.ini");
    const auto media = directory.filePath(QStringLiteral("中文 视频.mp4"));
    QFile video(media); QVERIFY(video.open(QIODevice::WriteOnly)); video.write("fake media"); video.close();
    Settings settings(file); settings.remember(media, 20000000, 90000000, false);
    QCOMPARE(settings.resumePosition(media), 20000000);
    settings.set("resumeEnabled", false); QCOMPARE(settings.resumePosition(media), 0);
    settings.set("resumeEnabled", true); settings.sync();
    Settings restored(file); QCOMPARE(restored.resumePosition(media), 20000000);
    settings.remember(media, 85000000, 90000000, false); QCOMPARE(settings.resumePosition(media), 0);
    settings.remember(media, 20000000, 90000000, true); QCOMPARE(settings.resumePosition(media), 0);
    settings.remember(media, 20000000, 90000000, false);
    QVERIFY(video.open(QIODevice::Append)); video.write("changed"); video.close(); QCOMPARE(settings.resumePosition(media), 0);
    for (int i = 0; i < 105; ++i) settings.remember(directory.filePath(QString::number(i)), 20000000, 90000000, false);
    QCOMPARE(settings.historySize(), 100); settings.clearHistory(); QCOMPARE(settings.historySize(), 0);
  }
  void controls() {
    auto stats = std::make_shared<AudioStats>();
    Controller controller(std::make_unique<FakeOutput>(stats));
    QSignalSpy failed(&controller, &Controller::failed);
    controller.open(fixture("av.mp4"));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy() && !controller.image().isNull(), 5000);
    QCOMPARE(controller.snapshot().state, sdk::PlayerState::playing);
    QTRY_VERIFY(stats->frames > 0);
    controller.togglePlay(); QTRY_VERIFY(!controller.busy());
    QCOMPARE(controller.snapshot().state, sdk::PlayerState::paused); QVERIFY(stats->paused);
    const auto frozen = controller.snapshot().position_us; const int queued = stats->pending;
    QTest::qWait(80); QCOMPARE(controller.snapshot().position_us, frozen); QCOMPARE(stats->pending, queued);
    const auto oldImage = controller.image(); const auto oldGeneration = controller.snapshot().generation;
    controller.seek(300000); controller.seek(600000); controller.seek(1200000);
    QTRY_VERIFY(controller.snapshot().generation > oldGeneration && !controller.busy() && !controller.image().isNull());
    QCOMPARE(controller.snapshot().position_us, 1200000); QCOMPARE(controller.snapshot().state, sdk::PlayerState::paused);
    QVERIFY(!oldImage.isNull()); QVERIFY(stats->resets > 0); QCOMPARE(stats->pending, 0);
    controller.togglePlay(); QTRY_VERIFY(!controller.busy() && controller.snapshot().state == sdk::PlayerState::playing);
    QVERIFY(stats->resumes > 0);
    controller.stop(); QTRY_VERIFY(!controller.busy() && controller.snapshot().state == sdk::PlayerState::ready);
    QCOMPARE(controller.snapshot().position_us, 0); QVERIFY(controller.image().isNull());
    controller.open(fixture("audio.wav")); QTRY_VERIFY(!controller.busy() && controller.snapshot().media.has_audio && !controller.snapshot().media.has_video);
    QVERIFY(controller.image().isNull()); QTRY_VERIFY_WITH_TIMEOUT(controller.drained(), 5000);
    controller.open(fixture("invalid.mp4")); QTRY_COMPARE(failed.count(), 1); QVERIFY(!controller.error().isEmpty());
    controller.open(fixture("video.mp4")); QTRY_VERIFY(!controller.busy() && !controller.image().isNull()); QVERIFY(controller.error().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(controller.drained(), 5000);
    controller.togglePlay(); QTRY_VERIFY(!controller.busy() && controller.snapshot().state == sdk::PlayerState::playing);
    controller.close(); QTRY_VERIFY(controller.finished()); QVERIFY(controller.image().isNull());
  }
  void rapidOpenAndClose() {
    Controller controller(std::make_unique<FakeOutput>());
    controller.open(fixture("av.mp4")); controller.open(fixture("audio.wav")); controller.open(fixture("video.mp4"));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy() && controller.path() == fixture("video.mp4") && !controller.image().isNull(), 5000);
    controller.open(fixture("av.mp4")); controller.close(); QTRY_VERIFY_WITH_TIMEOUT(controller.finished(), 5000);
  }
  void outputFailure() {
    auto stats = std::make_shared<AudioStats>(); Controller controller(std::make_unique<FakeOutput>(stats));
    controller.open(fixture("av.mp4")); QTRY_VERIFY(stats->frames > 0);
    stats->fail = true;
    QTRY_VERIFY_WITH_TIMEOUT(controller.drained(), 6000);
    QVERIFY(!controller.warning().isEmpty()); QVERIFY(controller.error().isEmpty());
    controller.close(); QTRY_VERIFY(controller.finished());
  }
  void subtitle() {
    QTemporaryDir directory; Controller controller(std::make_unique<FakeOutput>());
    controller.open(fixture("av.mp4")); QTRY_VERIFY(!controller.busy() && !controller.image().isNull());
    const auto path = directory.filePath(QStringLiteral("中文.srt")); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("1\n00:00:00,000 --> 00:00:02,000\n你好，Qt6\n第二行\n"); file.close();
    QVERIFY(controller.loadSubtitles(path)); const auto text = controller.subtitles.text_at(1000000); QVERIFY(!text.empty());
    QVERIFY(!controller.loadSubtitles(directory.filePath("missing.srt"))); QCOMPARE(controller.subtitles.text_at(1000000), text);
    controller.subtitles.delay_ms = 1000; QVERIFY(controller.subtitles.text_at(500000).empty());
    controller.open(fixture("audio.wav")); QVERIFY(controller.subtitles.size() == 0);
    controller.close(); QTRY_VERIFY(controller.finished());
  }
  void windowPlayback() {
    QTemporaryDir directory;
    PlayerWindow window(directory.filePath("settings.ini"), true, std::make_unique<FakeOutput>());
    window.show();
    window.openFiles({fixture("video.mp4"), fixture("audio.wav")});
    QTRY_VERIFY_WITH_TIMEOUT(window.playlist().rowForId(window.playlist().currentId()) == 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(window.controller().drained(), 5000);
    window.setAutoAdvance(false); window.openFiles({fixture("av.mp4")});
    QTRY_VERIFY(!window.controller().busy() && !window.controller().image().isNull());
    action(window, "play")->trigger(); QTRY_VERIFY(!window.controller().busy() && window.controller().snapshot().state == sdk::PlayerState::paused);
    QTest::keyClick(&window, Qt::Key_M); QVERIFY(action(window, "mute")->isChecked());
    const auto before = window.size(); QTest::keyClick(&window, Qt::Key_Escape); QVERIFY(window.isVisible()); QCOMPARE(window.size(), before);
    QDir().mkpath(PLAYER_SCREENSHOTS); QVERIFY(window.saveScreenshot(QString(PLAYER_SCREENSHOTS) + "/video.png"));
    window.controller().subtitles = Subtitles::parse("1\n00:00:00,000 --> 00:00:10,000\nQt6 字幕渲染测试\n第二行字幕\n");
    QTest::qWait(30); QVERIFY(window.saveScreenshot(QString(PLAYER_SCREENSHOTS) + "/subtitles.png"));
    window.openFiles({fixture("invalid.mp4")}); QTRY_VERIFY(!window.controller().error().isEmpty());
    QVERIFY(window.saveScreenshot(QString(PLAYER_SCREENSHOTS) + "/error.png"));
    window.close(); QTRY_VERIFY(window.controller().finished());
  }
  void failedPlaylist() {
    QTemporaryDir directory; PlayerWindow window(directory.filePath("settings.ini"), true, std::make_unique<FakeOutput>());
    action(window, "repeat2")->trigger();
    window.openFiles({fixture("invalid.mp4"), fixture("missing.mp4")});
    QSignalSpy failed(&window.controller(), &Controller::failed);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 2, 5000); QTest::qWait(120); QCOMPARE(failed.count(), 2);
    QVERIFY(!window.controller().busy());
    window.openFiles({fixture("invalid.mp4"), fixture("video.mp4")});
    QTRY_VERIFY(!window.controller().image().isNull());
    window.close(); QTRY_VERIFY(window.controller().finished());
  }
  void dragDropAndPersistence() {
    QTemporaryDir directory; const auto path = directory.filePath("settings.ini");
    {
      PlayerWindow window(path, false, std::make_unique<FakeOutput>()); window.setAutoAdvance(false); window.show();
      auto* video = window.findChild<VideoWidget*>("video"); QMimeData mime; mime.setUrls({QUrl::fromLocalFile(fixture("av.mp4"))});
      QDragEnterEvent enter(QPoint(50, 50), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(video, &enter); QVERIFY(enter.isAccepted());
      QDropEvent drop(QPointF(50, 50), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(video, &drop);
      QTRY_VERIFY(!window.controller().image().isNull());
      window.openFiles({fixture("audio.wav")}, true); QCOMPARE(window.playlist().rowCount(), 2);
      window.findChild<QSlider*>("volume")->setValue(37); action(window, "playlist")->setChecked(false); action(window, "repeat1")->trigger();
      window.close(); QTRY_VERIFY(window.controller().finished());
    }
    PlayerWindow restored(path, false, std::make_unique<FakeOutput>());
    QCOMPARE(restored.playlist().rowCount(), 2); QCOMPARE(restored.controller().snapshot().state, sdk::PlayerState::closed);
    QCOMPARE(restored.findChild<QSlider*>("volume")->value(), 37); QVERIFY(!action(restored, "playlist")->isChecked()); QVERIFY(action(restored, "repeat1")->isChecked());
    restored.close(); QTRY_VERIFY(restored.controller().finished());
  }
  void layout() {
    QTemporaryDir directory; PlayerWindow window(directory.filePath("settings.ini"), true, std::make_unique<FakeOutput>());
    window.show(); QTest::qWait(30); QDir().mkpath(PLAYER_SCREENSHOTS);
    auto* video = window.findChild<VideoWidget*>("video");
    const QString suffix = qEnvironmentVariable("QT_SCALE_FACTOR") == "2" ? "-2x" : "";
    QVERIFY(window.saveScreenshot(QString(PLAYER_SCREENSHOTS) + "/empty" + suffix + ".png"));
    window.resize(720, 480); QTest::qWait(30);
    QVERIFY(video->width() >= 200); QVERIFY(video->height() >= 180);
    auto* controls = window.findChild<QWidget*>("controls");
    for (auto* slider : controls->findChildren<QSlider*>()) {
      QVERIFY(slider->width() > 20); QVERIFY(controls->rect().contains(slider->mapTo(controls, slider->rect().bottomRight())));
    }
    QVERIFY(window.saveScreenshot(QString(PLAYER_SCREENSHOTS) + "/compact" + suffix + ".png"));
    action(window, "playlist")->trigger(); QVERIFY(window.findChild<QWidget*>("listPanel")->isHidden());
    QVERIFY(window.saveScreenshot(QString(PLAYER_SCREENSHOTS) + "/no-list" + suffix + ".png"));
    window.toggleFullscreen(); QVERIFY(window.isFullScreen());
    video->setFocus(); QTest::qWait(3300); QVERIFY(controls->isHidden());
    QTest::keyClick(&window, Qt::Key_Escape); QVERIFY(!window.isFullScreen()); QVERIFY(!controls->isHidden());
    window.close(); QTRY_VERIFY(window.controller().finished());
  }
};
QTEST_MAIN(PlayerTests)
#include "player_ui_tests.moc"
