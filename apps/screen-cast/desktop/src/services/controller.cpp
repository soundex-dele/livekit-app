#include "services/controller.hpp"
#include <algorithm>
#include <stdexcept>
namespace screen_cast {
QImage convertFrame(const livekit::sdk::VideoFrame &f, QSize limit) {
  if (f.width <= 0 || f.height <= 0 || f.width > 16384 || f.height > 16384 ||
      limit.isEmpty())
    throw std::invalid_argument("Invalid I420 dimensions");
  auto valid = [](const auto &p, int stride, int w, int h) {
    return stride >= w && p.size() >= size_t(stride) * h;
  };
  if (!valid(f.y, f.stride_y, f.width, f.height) ||
      !valid(f.u, f.stride_u, (f.width + 1) / 2, (f.height + 1) / 2) ||
      !valid(f.v, f.stride_v, (f.width + 1) / 2, (f.height + 1) / 2))
    throw std::invalid_argument("Invalid I420 planes");
  auto size = QSize(f.width, f.height);
  if (size.width() > limit.width() || size.height() > limit.height())
    size.scale(limit, Qt::KeepAspectRatio);
  QImage out(size, QImage::Format_RGB32);
  if (out.isNull())
    throw std::runtime_error("Cannot allocate preview");
  auto clip = [](int v) { return std::clamp(v, 0, 255); };
  for (int y = 0; y < size.height(); ++y) {
    auto *row = reinterpret_cast<QRgb *>(out.scanLine(y));
    int sy = int(int64_t(y) * f.height / size.height());
    for (int x = 0; x < size.width(); ++x) {
      int sx = int(int64_t(x) * f.width / size.width());
      int Y = std::max(0, int(f.y[sy * f.stride_y + sx]) - 16),
          U = int(f.u[(sy / 2) * f.stride_u + sx / 2]) - 128,
          V = int(f.v[(sy / 2) * f.stride_v + sx / 2]) - 128;
      row[x] = qRgb(clip((298 * Y + 409 * V + 128) >> 8),
                    clip((298 * Y - 100 * U - 208 * V + 128) >> 8),
                    clip((298 * Y + 516 * U + 128) >> 8));
    }
  }
  return out;
}
FrameConverter::FrameConverter()
    : worker_([this] {
        for (;;) {
          std::shared_ptr<const livekit::sdk::VideoFrame> frame;
          QSize limit;
          uint64_t epoch;
          {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return dirty_ || stop_; });
            if (stop_)
              return;
            frame = std::move(pending_);
            limit = limit_;
            epoch = epoch_;
            dirty_ = false;
          }
          QImage result;
          try {
            if (frame)
              result = convertFrame(*frame, limit);
          } catch (const std::exception &) {
          }
          {
            std::lock_guard lock(mutex_);
            if (epoch == epoch_)
              ready_ = std::move(result);
          }
        }
      }) {}
FrameConverter::~FrameConverter() {
  {
    std::lock_guard lock(mutex_);
    stop_ = true;
  }
  cv_.notify_one();
  worker_.join();
}
void FrameConverter::submit(
    std::shared_ptr<const livekit::sdk::VideoFrame> frame, QSize limit) {
  std::lock_guard lock(mutex_);
  if (frame == last_ && limit == limit_)
    return;
  last_ = frame;
  pending_ = std::move(frame);
  limit_ = limit;
  dirty_ = true;
  cv_.notify_one();
}
QImage FrameConverter::image() {
  std::lock_guard lock(mutex_);
  return ready_;
}
void FrameConverter::clear() {
  std::lock_guard lock(mutex_);
  ++epoch_;
  pending_.reset();
  last_.reset();
  ready_ = {};
  dirty_ = false;
}
Controller::Controller(Window &w, std::unique_ptr<Backend> b)
    : QObject(&w), window_(w), backend_(std::move(b)) {
  connect(&w, &Window::refreshRequested, this, [this] { refresh(); });
  connect(&w, &Window::startRequested, this, [this] {
    if (closing_ || !backend_->snapshot().finished)
      return;
    converter_.clear();
    lastState_.clear();
    stopping_ = false;
    try {
      backend_->start(window_.server().toStdString(),
                      window_.code().toStdString(), window_.selectedDevice());
    } catch (const std::exception &e) {
      const auto state = backend_->snapshot();
      lastState_ = state.state;
      lastError_ = state.error;
      lastFinished_ = state.finished;
      window_.present(state);
      window_.showError(readableError(e.what()));
    }
  });
  auto stop = [this] {
    stopping_ = true;
    converter_.clear();
    window_.setPreview({});
    backend_->stop();
  };
  connect(&w, &Window::stopRequested, this, stop);
  connect(&w, &Window::closeRequested, this, [this, stop] {
    closing_ = true;
    stop();
  });
  connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
  timer_.start(33);
  lastState_ = backend_->snapshot().state;
  refresh();
}
Controller::~Controller() {
  timer_.stop();
  backend_->stop();
  if (devices_.valid())
    devices_.wait();
}
void Controller::refresh() {
  if (devices_.valid() || closing_ || !backend_->snapshot().finished)
    return;
  window_.showError({});
  window_.setEnumerating(true);
  try {
    devices_ =
        std::async(std::launch::async, [this] { return backend_->devices(); });
  } catch (const std::exception &e) {
    window_.setEnumerating(false);
    window_.showError(readableError(e.what()));
  }
}
void Controller::tick() {
  if (devices_.valid() &&
      devices_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    try {
      window_.setDevices(devices_.get());
    } catch (const std::exception &e) {
      window_.setDevices({});
      window_.showError(readableError(e.what()));
    }
    window_.setEnumerating(false);
  }
  auto s = backend_->snapshot();
  if (s.state != lastState_ || s.error != lastError_ ||
      s.finished != lastFinished_) {
    lastState_ = s.state;
    lastError_ = s.error;
    lastFinished_ = s.finished;
    window_.present(s);
  }
  if (!s.finished && !stopping_ && s.state != "stopping" && s.preview) {
    converter_.submit(s.preview);
    window_.setPreview(converter_.image());
  } else {
    converter_.clear();
    window_.setPreview({});
  }
  if (closing_ && s.finished && !devices_.valid()) {
    timer_.stop();
    window_.finishClose();
  }
}
} // namespace screen_cast
