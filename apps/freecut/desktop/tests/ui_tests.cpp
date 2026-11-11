#include "window.hpp"
#include "timeline.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <algorithm>

class UiTests : public QObject {
  Q_OBJECT
 private slots:

  void empty() {
    freecut::Window window(true);
    window.show();
    QTest::qWait(100);
    QCOMPARE(window.document().project().tracks.size(), size_t(2));
    QVERIFY(window.findChild<QWidget*>("timeline"));
    QVERIFY(window.findChild<QPushButton*>("exportMp4")->isEnabled());
    QDir().mkpath(FREECUT_SCREENSHOTS);
    QVERIFY(window.grab().save(QString(FREECUT_SCREENSHOTS) + "/empty-" +
                               QString::number(window.devicePixelRatioF()) + ".png"));
  }

  void editing() {
    freecut::Window window(true);
    window.show();
    QSignalSpy imported(&window, &freecut::Window::imported);
    window.import_path(QString(FREECUT_FIXTURES) + "/red.mp4");
    QTRY_COMPARE_WITH_TIMEOUT(imported.count(), 1, 10000);
    QCOMPARE(window.document().project().assets.size(), size_t(1));
    QTest::mouseClick(window.findChild<QPushButton*>("insertAsset"), Qt::LeftButton);
    QCOMPARE(window.document().project().tracks[0].clips.size(), size_t(1));
    window.seek_frame(30);
    QTest::qWait(100);
    QTest::mouseClick(window.findChild<QPushButton*>("splitClip"), Qt::LeftButton);
    QCOMPARE(window.document().project().tracks[0].clips.size(), size_t(2));
    window.findChild<QSpinBox*>("clipPosition")->setValue(20);
    QTest::mouseClick(window.findChild<QPushButton*>("applyProperties"), Qt::LeftButton);
    QVERIFY(window.findChild<QLabel*>("notice")->text().contains(QStringLiteral("失败")));
    window.findChild<QSpinBox*>("clipPosition")->setValue(0);
    window.findChild<QSpinBox*>("clipDuration")->setValue(20);
    QTest::mouseClick(window.findChild<QPushButton*>("applyProperties"), Qt::LeftButton);
    QVERIFY(std::any_of(window.document().project().tracks[0].clips.begin(),
                        window.document().project().tracks[0].clips.end(), [](const auto& clip) {
                          return clip.position == 30 && clip.duration == 30;
                        }));
    const auto project = QString(FREECUT_SCREENSHOTS) + "/ui.freecut.json";
    QVERIFY(window.save_path(project));
    QVERIFY(!window.document().dirty());
    freecut::Window loaded(true);
    QVERIFY(loaded.load_path(project));
    QCOMPARE(loaded.document().project().tracks[0].clips.size(), size_t(2));
    QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QWidget*>("preview")->property("hasFrame").toBool(),
                             10000);
    QVERIFY(window.grab().save(QString(FREECUT_SCREENSHOTS) + "/editing.png"));
    QSignalSpy finished(&window, &freecut::Window::export_finished);
    window.export_path(QString(FREECUT_SCREENSHOTS) + "/ui-output.mp4");
    QVERIFY(window.exporting());
    QVERIFY(!window.findChild<QPushButton*>("exportMp4")->isEnabled());
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 60000);
    QVERIFY(finished.at(0).at(0).toBool());
    QVERIFY(window.findChild<QLabel*>("notice")->text().contains(QStringLiteral("导出完成")));
  }

  void errors() {
    freecut::Window window(true);
    QSignalSpy imported(&window, &freecut::Window::imported);
    window.import_path(QString(FREECUT_FIXTURES) + "/absent.mp4");
    QTRY_COMPARE_WITH_TIMEOUT(imported.count(), 1, 10000);
    QVERIFY(window.document().project().assets.empty());
    QVERIFY(window.findChild<QLabel*>("notice")->text().contains(QStringLiteral("失败")));
    QVERIFY(!window.load_path(QString(FREECUT_FIXTURES) + "/absent.freecut.json"));
    QSignalSpy finished(&window, &freecut::Window::export_finished);
    window.export_path(QString(FREECUT_SCREENSHOTS) + "/empty.mp4");
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QVERIFY(!finished.at(0).at(0).toBool());
  }

  void timeline_drag() {
    using namespace livekit::sdk;
    EditorProject p;
    p.assets.push_back({"asset", "clip", "example.mp4", 2000000, 160, 90, true, true, false});
    p.tracks = {{"v1", "Video 1", false, false, false, false, {{"clip", "asset", 10, 10, 20}}},
                {"v2", "Video 2", false, false, false, false, {}}};
    freecut::Timeline timeline;
    timeline.set_project(p);
    timeline.show();
    int edits = 0;
    timeline.edit = [&](auto project) {
      p = std::move(project);
      ++edits;
    };
    QTest::mousePress(&timeline, Qt::LeftButton, {}, {184, 132});
    QTest::mouseMove(&timeline, {214, 68});
    QTest::mouseRelease(&timeline, Qt::LeftButton, {}, {214, 68});
    QCOMPARE(edits, 1);
    QVERIFY(p.tracks[0].clips.empty());
    QCOMPARE(p.tracks[1].clips[0].position, int64_t(20));
    timeline.set_project(p);
    QTest::mousePress(&timeline, Qt::LeftButton, {}, {185, 68});
    QTest::mouseMove(&timeline, {191, 68});
    QTest::mouseRelease(&timeline, Qt::LeftButton, {}, {191, 68});
    QCOMPARE(edits, 2);
    QCOMPARE(p.tracks[1].clips[0].position, int64_t(22));
    QCOMPARE(p.tracks[1].clips[0].source_in, int64_t(12));
    QCOMPARE(p.tracks[1].clips[0].duration, int64_t(18));
    p.tracks[1].locked = true;
    timeline.set_project(p);
    QTest::mousePress(&timeline, Qt::LeftButton, {}, {200, 68});
    QTest::mouseMove(&timeline, {230, 68});
    QTest::mouseRelease(&timeline, Qt::LeftButton, {}, {230, 68});
    QCOMPARE(edits, 2);
    bool dropped = false;
    timeline.insert = [&](std::string asset, std::string track, int64_t frame) {
      dropped = asset == "asset" && track == "v1" && frame == 30;
    };
    QMimeData mime;
    mime.setData("application/x-freecut-asset", "asset");
    QDragEnterEvent enter({214, 132}, Qt::CopyAction, &mime, Qt::LeftButton, {});
    QApplication::sendEvent(&timeline, &enter);
    QDropEvent drop(QPointF(214, 132), Qt::CopyAction, &mime, Qt::LeftButton, {});
    QApplication::sendEvent(&timeline, &drop);
    QVERIFY(dropped);
    int64_t scrubbed = -1;
    timeline.seek = [&](int64_t frame) { scrubbed = frame; };
    QTest::mousePress(&timeline, Qt::LeftButton, {}, {154, 15});
    QTest::mouseMove(&timeline, {184, 15});
    QTest::mouseRelease(&timeline, Qt::LeftButton, {}, {184, 15});
    QCOMPARE(scrubbed, int64_t(20));
  }
};
QTEST_MAIN(UiTests)
#include "ui_tests.moc"
