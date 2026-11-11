#pragma once
#include "livekit/editor.hpp"
#include <QWidget>
#include <functional>
#include <set>
#include <map>
#include <QImage>

namespace freecut {
class Timeline final : public QWidget {
 public:
  explicit Timeline(QWidget* parent = nullptr);
  void set_project(const livekit::sdk::EditorProject& project);
  void set_playhead(int64_t frame);
  void set_zoom(double pixels_per_frame);
  void set_selected(std::string clip);
  std::function<void(int64_t)> seek;
  std::function<void(std::string, std::string)> select;
  std::function<void(livekit::sdk::EditorProject)> edit;
  std::function<void(std::string, std::string, int64_t)> insert;

  const std::set<std::string>& selection() const {
    return selection_;
  }

  void set_media(const std::string& asset, QImage image, std::vector<float> waveform);
  bool snapping = true;

 protected:
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void dragEnterEvent(QDragEnterEvent*) override;
  void dragMoveEvent(QDragMoveEvent*) override;
  void dropEvent(QDropEvent*) override;

 private:
  int track_at(double y) const;
  int64_t frame_at(double x) const;
  QRectF rect_for(int track, const livekit::sdk::EditorClip& clip) const;
  int64_t snap(int64_t frame, int64_t duration, int track) const;
  livekit::sdk::EditorProject project_, draft_;
  std::string selected_, drag_clip_;
  int drag_mode_ = 0, source_track_ = -1;
  bool scrubbing_ = false;
  QPointF press_;
  bool boxing_ = false;
  QRectF box_;
  std::set<std::string> selection_;
  std::map<std::string, QImage> thumbnails_;
  std::map<std::string, std::vector<float>> waveforms_;
  int64_t playhead_ = 0;
  double zoom_ = 3;
};
}  // namespace freecut
