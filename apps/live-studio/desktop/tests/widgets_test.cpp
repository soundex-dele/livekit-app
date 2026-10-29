#include "window.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace studio_app;

class WidgetsTest : public QObject {
  Q_OBJECT
 private Q_SLOTS:

  void initTestCase() {
    applyTheme();
  }

  void editAndRestore() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto root = std::filesystem::u8path(directory.path().toStdString());
    {
      Window window({root, true, false});
      window.show();
      QTRY_VERIFY_WITH_TIMEOUT(window.mediaSnapshot().frames > 0, 10000);
      auto* sources = window.findChild<QListWidget*>("sources");
      QCOMPARE(sources->count(), 2);
      sources->setCurrentRow(0);
      auto* x = window.findChild<QDoubleSpinBox*>("x");
      QVERIFY(x);
      x->setValue(.2);
      auto* preview = static_cast<Preview*>(window.findChild<QWidget*>("preview"));
      QVERIFY(preview);
      QTest::keyClick(preview, Qt::Key_Right);
      QCOMPARE(x->value(), .205);
      window.findChild<QCheckBox*>("locked")->setChecked(true);
      QTest::keyClick(preview, Qt::Key_Right);
      QCOMPARE(x->value(), .205);
      window.findChild<QCheckBox*>("locked")->setChecked(false);
      window.findChild<QPlainTextEdit*>("sourceText")
          ->setPlainText(QStringLiteral("Qt 6 中文测试"));
      window.findChild<QPushButton*>("sourceDown")->click();
      QCOMPARE(sources->currentRow(), 1);
      window.findChild<QPushButton*>("addScene")->click();
      QCOMPARE(window.findChild<QListWidget*>("scenes")->count(), 2);
      QTimer::singleShot(0, &window, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        dialog->findChild<QComboBox*>("sourceKind")->setCurrentIndex(6);
        dialog->findChild<QPushButton*>("confirmSource")->click();
      });
      window.findChild<QPushButton*>("addSource")->click();
      QCOMPARE(sources->count(), 1);
      QVERIFY(window.saveProject());
      size_t selected = 0;
      sdk::StudioOptions options;
      sdk::StudioAudioSettings audio;
      auto scenes = sdk::Studio::load_project(root / "project.json", selected, options, audio);
      QCOMPARE(scenes.size(), size_t(2));
      QCOMPARE(selected, size_t(1));
      QCOMPARE(scenes[0].sources[0].input, std::string("Qt 6 中文测试"));
      QVERIFY(std::abs(scenes[0].sources[0].transform.x - .205f) < .001f);
      QVERIFY(!window.findChild<QPushButton*>("startLive")->isEnabled());
      QVERIFY(!window.findChild<QCheckBox*>("microphone")->isEnabled());
    }
    Window restored({root, true, false});
    restored.show();
    QCOMPARE(restored.findChild<QListWidget*>("scenes")->count(), 2);
    QCOMPARE(restored.findChild<QListWidget*>("sources")->count(), 1);
    restored.findChild<QPushButton*>("deleteScene")->click();
    QCOMPARE(restored.findChild<QListWidget*>("scenes")->count(), 1);
    QVERIFY(!restored.findChild<QPushButton*>("deleteScene")->isEnabled());
  }

  void offlineProjectNeverCaptures() {
    QTemporaryDir directory;
    auto root = std::filesystem::u8path(directory.path().toStdString());
    sdk::StudioScene scene;
    sdk::StudioSource camera;
    camera.id = "camera";
    camera.kind = sdk::StudioSourceKind::camera;
    camera.input = "must-not-open";
    scene.sources.push_back(camera);
    sdk::StudioAudioSettings audio;
    audio.microphone = audio.system = audio.monitor = true;
    sdk::Studio::save_project(root / "project.json", {scene}, 0, {}, audio);
    Window window({root, true, false});
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.mediaSnapshot().frames > 1, 10000);
    QVERIFY(window.mediaSnapshot().source_errors.empty());
    QVERIFY(window.mediaSnapshot().error.empty());
    size_t selected = 0;
    sdk::StudioOptions options;
    auto saved = sdk::Studio::load_project(root / "project.json", selected, options, audio);
    QCOMPARE(saved[0].sources.size(), size_t(1));
    QVERIFY(audio.microphone && audio.system && audio.monitor);
  }

  void portraitAndLayout() {
    QTemporaryDir directory;
    Window window({std::filesystem::u8path(directory.path().toStdString()), true, true});
    window.resize(1080, 700);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.mediaSnapshot().preview != nullptr, 10000);
    QCOMPARE(window.mediaSnapshot().preview->width, 720);
    QCOMPARE(window.mediaSnapshot().preview->height, 1280);
    auto* preview = static_cast<Preview*>(window.findChild<QWidget*>("preview"));
    QVERIFY(preview->width() >= 280);
    QCOMPARE(window.width(), 1080);
    // Engine publication and the 33 ms GUI refresh happen on different threads.
    QTRY_VERIFY_WITH_TIMEOUT(
        std::abs(preview->canvasRect().width() / preview->canvasRect().height() - 720.0 / 1280) <
            .001,
        10000);
    auto* format = window.findChild<QComboBox*>("format");
    format->setCurrentIndex(0);
    QMetaObject::invokeMethod(format, "activated", Q_ARG(int, 0));
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
                               auto media = window.mediaSnapshot();
                               return media.preview && media.preview->width == 1280;
                             })(),
                             10000);
  }
};

int main(int argc, char** argv) {
  configureOffscreenPlatform();
  QApplication app(argc, argv);
  WidgetsTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "widgets_test.moc"
