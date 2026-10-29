#pragma once
#include <functional>
#include <memory>
#include <string>

namespace remote_app {
class Platform {
 public:
  explicit Platform(std::function<void()> stop);
  ~Platform();
  std::string hotkey() const;
  static void request_screen_permission();
  static void request_input_permission();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace remote_app
