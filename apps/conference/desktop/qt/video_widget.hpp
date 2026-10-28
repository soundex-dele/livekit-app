#pragma once
#include <QImage>
#include <QWidget>
#include <condition_variable>
#include <livekit/media.hpp>
#include <map>
#include <mutex>
#include <thread>
namespace meeting {
QImage convert_frame(const livekit::sdk::VideoFrame &frame,
                     QSize limit = {1280, 720});
QRect fit_video(QRect bounds, QSize source);
class VideoWidget : public QWidget {
public:
  explicit VideoWidget(QWidget *parent = nullptr);
  void present(QImage image, QString name, QString status,
               bool speaker = false);

protected:
  void paintEvent(QPaintEvent *) override;

private:
  QImage image_;
  QString name_, status_;
  bool speaker_ = false;
};
// The GUI replaces the pending batch. The worker never queues a Qt event per
// frame.
class FrameConverter {
public:
  using Frames =
      std::map<std::string, std::shared_ptr<const livekit::sdk::VideoFrame>>;
  FrameConverter();
  ~FrameConverter();
  void submit(Frames frames, QSize limit);
  std::map<std::string, QImage> images();
  void clear();

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  Frames pending_, last_;
  std::map<std::string, QImage> ready_;
  QSize limit_{640, 360};
  bool dirty_ = false, stop_ = false;
  uint64_t epoch_ = 0;
  std::thread worker_;
};
} // namespace meeting
