#include "services/controller.hpp"
#include <QCoreApplication>
#include <fstream>
#include <iostream>
using namespace std::chrono_literals;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (argc < 4)
    return 2;
  try {
    remote_app::Controller controller;
    controller.start(argv[1], argv[2], "Native fixture");
    bool sent = false;
    auto deadline = std::chrono::steady_clock::now() + 30s;
    while (std::chrono::steady_clock::now() < deadline) {
      auto s = controller.snapshot();
      if (s.finished)
        throw std::runtime_error(s.error.empty() ? "Session ended early" : s.error);
      if (s.layout && s.frames >= 5 && !sent) {
        std::cout << "FRAMES " << s.frames << std::endl;
        controller.input(
            {{"type", "button"}, {"button", 0}, {"down", true}, {"x", 0.5}, {"y", 0.5}});
        controller.input(
            {{"type", "button"}, {"button", 0}, {"down", false}, {"x", 0.5}, {"y", 0.5}});
        controller.input({{"type", "key"}, {"code", "ShiftLeft"}, {"down", true}});
        controller.release();
        controller.send_text(std::string("你好，Qt 远程协助 😀\n第二行\n") +
                             (argc > 5 ? argv[5] : "fixture"));
        sent = true;
      }
      if (sent && s.text_accepted) {
        std::cout << "TEXT_ACCEPTED" << std::endl;
        auto mode = argc > 4 ? std::string(argv[4]) : "stop";
        if (mode == "wait-failure") {
          controller.input({{"type", "key"}, {"code", "ControlLeft"}, {"down", true}});
          std::cout << "HOLDING" << std::endl;
          auto end = std::chrono::steady_clock::now() + 15s;
          while (!controller.snapshot().finished && std::chrono::steady_clock::now() < end)
            std::this_thread::sleep_for(20ms);
          if (!controller.snapshot().finished)
            throw std::runtime_error("Failure did not stop session");
        } else {
          controller.stop();
          auto end = std::chrono::steady_clock::now() + 10s;
          while (!controller.snapshot().finished && std::chrono::steady_clock::now() < end)
            std::this_thread::sleep_for(20ms);
          if (!controller.snapshot().finished)
            throw std::runtime_error("Stop did not complete");
        }
        std::ofstream out(argv[3]);
        out << livekit::sdk::Json{{"synthetic_media", true},
                                  {"native_input", false},
                                  {"frames", s.frames},
                                  {"text_accepted", true},
                                  {"finished", controller.snapshot().finished},
                                  {"error", controller.snapshot().error}}
                   .dump(2);
        return 0;
      }
      std::this_thread::sleep_for(10ms);
    }
    throw std::runtime_error(
        "30 seconds: at least five decoded frames and "
        "text acknowledgement required");
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
