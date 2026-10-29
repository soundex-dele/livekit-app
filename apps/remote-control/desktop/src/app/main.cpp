#include "features/remote_control/ui.hpp"
#include <QApplication>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("AVPlatform");
  QCoreApplication::setApplicationName("RemoteControl");
  remote_app::apply_theme();
  const bool smoke = app.arguments().contains("--smoke-test");
  remote_app::MainWindow window(smoke);
  window.show();
  if (smoke)
    QTimer::singleShot(100, &app, &QCoreApplication::quit);
  return app.exec();
}
