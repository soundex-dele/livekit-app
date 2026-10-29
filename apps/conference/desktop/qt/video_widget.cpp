#include "video_widget.hpp"
#include <QPainter>
#include <algorithm>
#include <stdexcept>

namespace meeting {
QRect fit_video(QRect b, QSize s) {
  if (s.isEmpty())
    return {};
  auto size = s.scaled(b.size(), Qt::KeepAspectRatio);
  return {b.center() - QPoint(size.width() / 2, size.height() / 2), size};
}

QImage convert_frame(const livekit::sdk::VideoFrame& f, QSize limit) {
  if (f.width <= 0 || f.height <= 0 || f.width > 16384 || f.height > 16384)
    throw std::invalid_argument("Invalid I420 dimensions");
  auto valid = [](const auto& p, int stride, int w, int h) {
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
    throw std::runtime_error("Cannot allocate video image");
  auto clip = [](int v) { return std::clamp(v, 0, 255); };
  for (int y = 0; y < size.height(); ++y) {
    auto* row = reinterpret_cast<QRgb*>(out.scanLine(y));
    int sy = int(int64_t(y) * f.height / size.height());
    for (int x = 0; x < size.width(); ++x) {
      int sx = int(int64_t(x) * f.width / size.width());
      int Y = std::max(0, int(f.y[sy * f.stride_y + sx]) - 16),
          U = int(f.u[(sy / 2) * f.stride_u + sx / 2]) - 128,
          V = int(f.v[(sy / 2) * f.stride_v + sx / 2]) - 128;
      row[x] =
          qRgb(clip((298 * Y + 409 * V + 128) >> 8), clip((298 * Y - 100 * U - 208 * V + 128) >> 8),
               clip((298 * Y + 516 * U + 128) >> 8));
    }
  }
  return out;
}

VideoWidget::VideoWidget(QWidget* p) : QWidget(p) {
  setObjectName("videoTile");
  setMinimumSize(120, 90);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setAccessibleName("参会者画面");
}

void VideoWidget::present(QImage image, QString name, QString status, bool speaker) {
  image_ = std::move(image);
  name_ = std::move(name);
  status_ = std::move(status);
  speaker_ = speaker;
  setAccessibleName(name_ + " " + status_);
  update();
}

void VideoWidget::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.fillRect(rect(), QColor("#222833"));
  if (!image_.isNull()) {
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(fit_video(rect(), image_.size()), image_);
  } else {
    int diameter = std::clamp(height() - 78, 24, 64);
    QRect avatar(0, 0, diameter, diameter);
    avatar.moveCenter(QPoint(width() / 2, (height() - 30) / 2 - 10));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#315b97"));
    p.drawEllipse(avatar);
    p.setPen(Qt::white);
    QFont font = p.font();
    font.setPixelSize(diameter > 40 ? 27 : 18);
    p.setFont(font);
    p.drawText(avatar, Qt::AlignCenter, name_.left(1));
    font.setPixelSize(12);
    p.setFont(font);
    p.setPen(QColor("#c6cfdd"));
    p.drawText(QRect(4, avatar.bottom() + 6, width() - 8, 18), Qt::AlignCenter, status_);
  }
  p.fillRect(0, height() - 30, width(), 30, QColor(15, 19, 26, 210));
  p.setPen(Qt::white);
  auto f = p.font();
  f.setPixelSize(12);
  p.setFont(f);
  p.drawText(QRect(10, height() - 30, width() - 20, 30), Qt::AlignVCenter,
             p.fontMetrics().elidedText(name_, Qt::ElideRight, width() - 20));
  if (speaker_) {
    p.setPen(QPen(QColor("#5bb5ff"), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect().adjusted(1, 1, -1, -1));
  }
}

FrameConverter::FrameConverter()
    : worker_([this] {
        for (;;) {
          Frames next;
          QSize size;
          uint64_t epoch;
          {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [&] { return dirty_ || stop_; });
            if (stop_)
              return;
            next = std::move(pending_);
            size = limit_;
            epoch = epoch_;
            dirty_ = false;
          }
          std::map<std::string, QImage> result;
          for (auto& [id, frame] : next) {
            if (!frame)
              continue;
            try {
              result[id] = convert_frame(*frame, size);
            } catch (const std::exception&) {
            }
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

void FrameConverter::submit(Frames f, QSize size) {
  std::lock_guard lock(mutex_);
  if (f == last_ && size == limit_)
    return;
  last_ = f;
  pending_ = std::move(f);
  limit_ = size;
  dirty_ = true;
  cv_.notify_one();
}

std::map<std::string, QImage> FrameConverter::images() {
  std::lock_guard lock(mutex_);
  return ready_;
}

void FrameConverter::clear() {
  std::lock_guard lock(mutex_);
  ++epoch_;
  pending_.clear();
  last_.clear();
  ready_.clear();
  dirty_ = false;
}
}  // namespace meeting
