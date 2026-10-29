#pragma once
#include "features/ui.hpp"
#include <QObject>
#include <QTimer>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

namespace screen_cast {
QImage convertFrame(const livekit::sdk::VideoFrame& frame, QSize limit);

class FrameConverter {
 public:
  FrameConverter();
  ~FrameConverter();
  void submit(std::shared_ptr<const livekit::sdk::VideoFrame> frame, QSize limit = {960, 720});
  QImage image();
  void clear();

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stop_ = false, dirty_ = false;
  uint64_t epoch_ = 0;
  QSize limit_;
  std::shared_ptr<const livekit::sdk::VideoFrame> pending_, last_;
  QImage ready_;
  std::thread worker_;
};

// Injectable boundary keeps automated UI tests away from actual display
// capture.
class Backend {
 public:
  virtual ~Backend() = default;
  virtual std::vector<livekit::sdk::ScreenCaptureDevice> devices() = 0;
  virtual void start(std::string url, std::string code,
                     livekit::sdk::ScreenCaptureDevice display) = 0;
  virtual void stop() = 0;
  virtual Snapshot snapshot() const = 0;
};

class Controller final : public QObject {
 public:
  Controller(Window& window, std::unique_ptr<Backend> backend);
  ~Controller() override;

 private:
  void refresh();
  void tick();
  Window& window_;
  std::unique_ptr<Backend> backend_;
  QTimer timer_;
  FrameConverter converter_;
  std::future<std::vector<livekit::sdk::ScreenCaptureDevice>> devices_;
  bool closing_ = false, stopping_ = false;
  std::string lastState_, lastError_;
  bool lastFinished_ = true;
};

std::unique_ptr<Backend> nativeBackend();
}  // namespace screen_cast
