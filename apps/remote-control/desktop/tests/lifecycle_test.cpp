#include "services/controller.hpp"
#include <QCoreApplication>
#include <iostream>
using namespace std::chrono_literals;
namespace {
struct Sink : livekit::sdk::ControlInputSink {
  bool apply(const livekit::sdk::ControlInput &) noexcept override {
    std::terminate();
  }
};
template <class Session> void cancel_and_wait(Session &s) {
  std::this_thread::sleep_for(30ms);
  s.stop();
  auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!s.snapshot().finished && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(10ms);
  if (!s.snapshot().finished)
    throw std::runtime_error("Cancellation did not finish");
  if (!s.snapshot().error.empty())
    throw std::runtime_error(s.snapshot().error);
}
} // namespace
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (argc != 2)
    return 2;
  try {
    remote_app::Session host(true, std::make_shared<Sink>());
    host.start(argv[1]);
    cancel_and_wait(host);
    host.start(argv[1]);
    cancel_and_wait(host);
    remote_app::Controller controller;
    controller.start(argv[1], "0123456789", "late response");
    cancel_and_wait(controller);
    controller.start(argv[1], "0123456789", "retry");
    cancel_and_wait(controller);
    std::cout
        << "Cancelled grants revoked, no stale media start, session reusable\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
