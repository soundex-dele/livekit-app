#include "services/controller.hpp"
#include "services/session.hpp"

namespace screen_cast {
namespace {
class NativeBackend final : public Backend {
  std::vector<livekit::sdk::ScreenCaptureDevice> devices() override {
    return livekit::sdk::screen_capture_devices();
  }

  void start(std::string url, std::string code,
             livekit::sdk::ScreenCaptureDevice display) override {
    session_.start(std::move(url), std::move(code), std::move(display));
  }

  void stop() override {
    session_.stop();
  }

  Snapshot snapshot() const override {
    return session_.snapshot();
  }

  Session session_;
};
}  // namespace

std::unique_ptr<Backend> nativeBackend() {
  return std::make_unique<NativeBackend>();
}
}  // namespace screen_cast
