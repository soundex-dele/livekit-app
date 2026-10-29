#include "services/session.hpp"
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
using namespace std::chrono_literals;

namespace {
void wait(screen_cast::Session& s) {
  auto end = std::chrono::steady_clock::now() + 8s;
  while (!s.snapshot().finished && std::chrono::steady_clock::now() < end)
    std::this_thread::sleep_for(10ms);
  if (!s.snapshot().finished || s.snapshot().preview)
    throw std::runtime_error("Session did not release resources");
}
}  // namespace

int main() {
  try {
    screen_cast::Session session(true);
    for (int i = 0; i < 5; ++i) {
      session.start("http://127.0.0.1:1", "1234567890",
                    {"fixture", "fixture", 0, 0, 1280, 720, true});
      session.stop();
      wait(session);
    }
    // Permission denial occurs before either screen enumeration or HTTP
    // redemption.
    screen_cast::Session denied(false, [] { return false; });
    denied.start("invalid-service", "0123456789", {});
    wait(denied);
    if (denied.snapshot().error != "Screen recording permission denied")
      throw std::runtime_error("Permission preflight was bypassed");
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    screen_cast::Session cancelled(false, [&] {
      entered.set_value();
      released.wait();
      return true;
    });
    cancelled.start("invalid-service", "0123456789", {});
    entered.get_future().wait();
    cancelled.stop();
    release.set_value();
    wait(cancelled);
    if (!cancelled.snapshot().error.empty())
      throw std::runtime_error("Cancelled permission attempted further work");
    std::cout << "Repeated cancellation, permission denial and cancellation "
                 "before redemption passed (no real capture)\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
