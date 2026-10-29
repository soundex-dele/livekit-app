#include "qt/window.hpp"
#include <QApplication>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  app.setOrganizationName("AVPlatform");
  app.setApplicationName("AVMeeting");
  app.setApplicationDisplayName("AV 会议");
  meeting::Window window;
  window.show();
  return app.exec();
}
