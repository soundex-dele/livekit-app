#include "window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QTimer>

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (QByteArray(argv[i]) == "--hidden" && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
      qputenv("QT_QPA_PLATFORM", "offscreen");
    }
  }
  QApplication app(argc, argv);
  app.setApplicationName("FreeCut");
  app.setOrganizationName("AV Platform");
  app.setStyle("Fusion");
  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("FreeCut 多轨剪辑器"));
  parser.addHelpOption();
  parser.addOption({"hidden", "Keep the window off screen (automated verification)."});
  parser.addOption({"smoke", "Exit after an offscreen startup smoke check."});
  parser.addOption({"screenshot", "Save a program-rendered screenshot.", "path"});
  parser.addPositionalArgument("project", "Optional .freecut.json project or local media file.");
  parser.process(app);
  freecut::Window window(parser.isSet("hidden"));
  if (parser.isSet("hidden")) {
    window.setAttribute(Qt::WA_DontShowOnScreen);
  }
  const auto args = parser.positionalArguments();
  if (!args.empty()) {
    if (args.first().endsWith(".freecut.json")) {
      window.load_path(args.first());
    } else {
      window.import_path(args.first());
    }
  }
  window.show();
  if (parser.isSet("smoke") || parser.isSet("screenshot")) {
    QTimer::singleShot(1000, &app, [&] {
      const bool saved =
          !parser.isSet("screenshot") || window.grab().save(parser.value("screenshot"));
      app.exit(saved ? 0 : 1);
    });
  }
  return app.exec();
}
