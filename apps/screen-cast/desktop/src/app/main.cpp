#include "features/ui.hpp"
#include "services/controller.hpp"
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setOrganizationName("AVPlatform");
  app.setApplicationName("ScreenCast");
  app.setApplicationDisplayName("局域网传屏");
  // Packaged-runtime check never enumerates or captures the user's desktop.
  QTemporaryDir temporarySettings;
  const bool check = app.arguments().contains("--check-runtime");
  if (check) {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       temporarySettings.path());
  }
  screen_cast::Window window;
  if (check) {
    QImage image(window.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    window.render(&image);
    std::cout << "Qt Widgets runtime ready (no device access)\n";
    return image.isNull() ? 1 : 0;
  }
  screen_cast::Controller controller(window, screen_cast::nativeBackend());
  window.show();
  return app.exec();
}
