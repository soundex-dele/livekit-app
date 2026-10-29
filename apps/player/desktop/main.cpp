#include "window.hpp"
#include "utoolkit/logging/logger.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>

namespace {
class SilentOutput final : public player_demo::AudioOutput {
 public:
  void open() override {}

  bool canAccept() const override {
    return true;
  }

  void enqueue(const livekit::sdk::media::AudioFrame&) override {}

  void pump() override {}

  void suspend(bool) override {}

  void reset() override {}

  void setVolume(float) override {}

  bool drained() const override {
    return true;
  }

  QString warning() const override {
    return {};
  }
};
}  // namespace

int main(int argc, char** argv) {
  // Headless CLI runs never connect to the user's desktop or sound device.
  for (int i = 1; i < argc; ++i)
    if (QByteArray(argv[i]) == "--hidden" && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
      qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QApplication::setApplicationName("livekit_local_player");
  QApplication::setOrganizationName("LiveKit");
  QApplication::setQuitOnLastWindowClosed(false);
  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("Qt6 本地文件播放器"));
  parser.addHelpOption();
  parser.addPositionalArgument("file", QStringLiteral("本地媒体文件"), "[file]");
  parser.addOptions({{"auto-exit", QStringLiteral("播放结束后退出")},
                     {"hidden", QStringLiteral("无窗口、无设备输出测试")},
                     {"verbose", QStringLiteral("输出详细媒体诊断")},
                     {"frames", QStringLiteral("更新循环次数"), "count"},
                     {"screenshot", QStringLiteral("导出程序内部渲染截图"), "path"}});
  parser.process(app);
  bool validFrames = true;
  int frames = parser.isSet("frames") ? parser.value("frames").toInt(&validFrames) : 0;
  const auto paths = parser.positionalArguments();
  if (!validFrames || frames < 0 || paths.size() > 1 ||
      (parser.isSet("auto-exit") && paths.isEmpty())) {
    std::cerr << "参数错误：--auto-exit 需要媒体文件，--frames 必须为非负整数。\n";
    return 2;
  }
  const bool hidden = parser.isSet("hidden");
  const bool automation =
      hidden || parser.isSet("auto-exit") || parser.isSet("frames") || parser.isSet("screenshot");
  QTemporaryDir temp;
  if (automation && !temp.isValid()) {
    std::cerr << "无法创建测试设置目录\n";
    return 2;
  }
  utoolkit::logging::Logger::GetInstance().Initialize(
      "player", "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v",
      parser.isSet("verbose") ? utoolkit::logging::LogLevel::Trace
                              : utoolkit::logging::LogLevel::Warn);
  player_demo::PlayerWindow window(automation ? temp.filePath("settings.ini") : QString{},
                                   automation, hidden ? std::make_unique<SilentOutput>() : nullptr);
  window.setAutoAdvance(!parser.isSet("auto-exit"));
  int code = 0, ticks = 0;
  bool captured = false;
  QObject::connect(&window, &player_demo::PlayerWindow::shutdownComplete, &app,
                   [&] { app.exit(code); });
  if (parser.isSet("auto-exit")) {
    QObject::connect(&window.controller(), &player_demo::Controller::ended, &window,
                     [&] { window.close(); });
    QObject::connect(&window.controller(), &player_demo::Controller::failed, &window,
                     [&](const QString& error) {
                       std::cerr << error.toStdString() << '\n';
                       code = 1;
                       window.close();
                     });
  }
  QObject::connect(&window.controller(), &player_demo::Controller::ticked, &window, [&] {
    ++ticks;
    const auto media = window.controller().snapshot().media;
    const bool ready = paths.isEmpty() || !window.controller().image().isNull() ||
                       !window.controller().error().isEmpty() ||
                       (!window.controller().busy() && media.has_audio && !media.has_video);
    if (parser.isSet("screenshot") && !captured && ticks > 3 && ready) {
      captured = window.saveScreenshot(parser.value("screenshot"));
      if (!captured) {
        code = 1;
        std::cerr << "截图写入失败\n";
        window.close();
      }
    }
    if (frames > 0 && ticks >= frames) {
      if (parser.isSet("screenshot") && !captured) {
        code = 1;
        std::cerr << "循环结束前未生成截图\n";
      }
      window.close();
    }
  });
  if (!hidden)
    window.show();
  if (!paths.isEmpty())
    window.openFiles(paths);
  return app.exec();
}
