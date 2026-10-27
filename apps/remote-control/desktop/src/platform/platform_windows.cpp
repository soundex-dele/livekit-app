#include "infrastructure/emergency_hotkey.hpp"
#include "platform.hpp"
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <windows.h>
namespace remote_app {
struct Platform::Impl : QAbstractNativeEventFilter {
  std::function<void()> stop;
  EmergencyHotkey key;
  bool nativeEventFilter(const QByteArray &, void *message,
                         qintptr *) override {
    auto m = static_cast<MSG *>(message);
    if (m->message == WM_HOTKEY && m->wParam == 0x4156) {
      stop();
      return true;
    }
    return false;
  }
};
Platform::Platform(std::function<void()> stop)
    : impl_(std::make_unique<Impl>()) {
  impl_->stop = std::move(stop);
  impl_->key =
      register_emergency_hotkey([](unsigned key, unsigned long &error) {
        if (RegisterHotKey(nullptr, 0x4156,
                           MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, key))
          return true;
        error = GetLastError();
        return false;
      });
  QCoreApplication::instance()->installNativeEventFilter(impl_.get());
}
Platform::~Platform() {
  QCoreApplication::instance()->removeNativeEventFilter(impl_.get());
  if (impl_->key.available())
    UnregisterHotKey(nullptr, 0x4156);
}
std::string Platform::hotkey() const {
  return impl_->key.available() ? impl_->key.label : "";
}
void Platform::request_screen_permission() {}
void Platform::request_input_permission() {}
} // namespace remote_app
