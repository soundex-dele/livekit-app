#include "features/remote_control/ui.hpp"
#include <QApplication>
#include <QDir>
#include <QTest>
#include <algorithm>
#include <iostream>
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  remote_app::apply_theme();
  try {
    auto check = [](bool x, const char *m) {
      if (!x)
        throw std::runtime_error(m);
    };
    remote_app::MainWindow window(true);
    window.show();
    for (const auto *state :
         {"idle", "waiting", "pending", "approved", "ended"}) {
      remote_app::Snapshot s;
      s.state = state;
      s.code = "0123456789";
      s.name = "协助者小明";
      s.finished = s.state == "idle" || s.state == "ended";
      window.display(s, {}, {true, true, true});
      app.processEvents();
      check(window.grab().save(QString("remote-host-%1.png").arg(state)),
            "save screenshot");
      check(window.findChild<QPushButton *>("approve")->isVisible() ==
                (s.state == "pending"),
            "approval visibility");
      check(window.findChild<QPushButton *>("createCode")->isEnabled() ==
                (s.finished || s.state == "waiting"),
            "regenerate only before a request or after cleanup");
    }
    window.display({}, {}, {false, false, true});
    app.processEvents();
    check(window.grab().save("remote-host-permissions.png"),
          "permission screenshot");
    check(remote_app::video_point({0, 0, 800, 600}, {1600, 900}, {400, 300})
              .has_value(),
          "center");
    check(!remote_app::video_point({0, 0, 800, 600}, {1600, 900}, {400, 10}),
          "letterbox excluded");
    remote_app::Snapshot pending;
    pending.state = "pending";
    pending.finished = false;
    pending.name = "权限测试";
    window.display(pending, {}, {false, false, true});
    app.processEvents();
    check(!window.findChild<QPushButton *>("approve")->isEnabled(),
          "permission blocks approval");
    remote_app::Snapshot failure;
    failure.state = "ended";
    failure.error = "业务服务不可达，请检查服务地址后重试";
    window.display(failure, {}, {true, true, true});
    app.processEvents();
    check(window.grab().save("remote-host-error.png"), "error screenshot");
    remote_app::VideoWidget surface;
    surface.resize(800, 600);
    QImage pixels(1600, 900, QImage::Format_RGB32);
    pixels.fill(Qt::black);
    std::vector<livekit::sdk::Json> recorded;
    int releases = 0;
    surface.send = [&](auto event) { recorded.push_back(event); };
    surface.released = [&] { ++releases; };
    surface.present(pixels, true, false, 1);
    surface.show();
    app.processEvents();
    QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier,
                      QPoint(400, 20));
    check(recorded.empty(), "letterbox does not inject");
    QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier,
                      QPoint(400, 300));
    check(recorded.size() == 2, "content click forwarded");
    // QTest's synthesized spontaneous Cocoa event carries nativeVirtualKey=0
    // (a real physical A). Send a non-native event to test focus traversal.
    QKeyEvent backtabDown(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
    QKeyEvent backtabUp(QEvent::KeyRelease, Qt::Key_Backtab, Qt::ShiftModifier);
    QApplication::sendEvent(&surface, &backtabDown);
    QApplication::sendEvent(&surface, &backtabUp);
    check(std::any_of(recorded.begin(), recorded.end(), [](const auto &e) {
            return e.value("code", "") == "Tab" && e.value("down", false);
          }), "shift-tab reaches remote instead of traversing local focus");
    QTest::keyPress(&surface, Qt::Key_Shift);
    auto count = recorded.size();
    surface.pause();
    check(releases > 0 && !surface.target(),
          "pause releases and clears target");
    QTest::keyPress(&surface, Qt::Key_A);
    check(recorded.size() == count, "paused input ignored");
    remote_app::Controller controller;
    remote_app::ControlWindow view(controller);
    remote_app::ControllerSnapshot c;
    c.finished = false;
    c.layout = 1;
    c.width = 1280;
    c.height = 720;
    c.text_supported = true;
    c.image = QImage(640, 360, QImage::Format_RGB32);
    c.image.fill(QColor("#3b638c"));
    view.present(c);
    view.show();
    app.processEvents();
    check(view.grab().save("remote-controller.png"), "controller screenshot");
    std::cout << "Qt offscreen render and geometry checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
