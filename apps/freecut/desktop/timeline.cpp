#include "timeline.hpp"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace freecut {
using namespace livekit::sdk;

namespace {
constexpr int kHeader = 124, kRuler = 36, kRow = 64;
}

Timeline::Timeline(QWidget* parent) : QWidget(parent) {
  setObjectName("timeline");
  setAcceptDrops(true);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setAccessibleName(QStringLiteral("多轨时间线"));
}

void Timeline::set_project(const EditorProject& p) {
  project_ = p;
  draft_ = p;
  const auto end = std::max<int64_t>(editor_duration(p) + 90, 300);
  zoom_ = std::min(zoom_, (16000000. - kHeader) / end);
  setMinimumSize(kHeader + int(end * zoom_), kRuler + std::max(2, int(p.tracks.size())) * kRow);
  update();
}

void Timeline::set_playhead(int64_t frame) {
  playhead_ = frame;
  update();
}

void Timeline::set_zoom(double zoom) {
  zoom_ = std::clamp(zoom, .1, 20.);
  set_project(project_);
}

void Timeline::set_selected(std::string id) {
  selected_ = std::move(id);
  selection_.clear();
  if (!selected_.empty()) {
    selection_.insert(selected_);
  }
  update();
}

void Timeline::set_media(const std::string& asset, QImage image, std::vector<float> waveform) {
  thumbnails_[asset] = std::move(image);
  waveforms_[asset] = std::move(waveform);
  update();
}

int Timeline::track_at(double y) const {
  const int row = int(std::floor((y - kRuler) / kRow));
  return row < 0 || row >= int(project_.tracks.size()) ? -1 : int(project_.tracks.size()) - row - 1;
}

int64_t Timeline::frame_at(double x) const {
  return std::max<int64_t>(0, std::llround((x - kHeader) / zoom_));
}

QRectF Timeline::rect_for(int track, const EditorClip& c) const {
  return {kHeader + c.position * zoom_,
          kRuler + (int(project_.tracks.size()) - track - 1) * kRow + 6.,
          std::max(3., c.duration * zoom_), kRow - 12.};
}

int64_t Timeline::snap(int64_t frame, int64_t duration, int track) const {
  if (!snapping || track < 0) {
    return frame;
  }
  int64_t best = frame;
  double distance = 9. / zoom_;
  auto consider = [&](int64_t target) {
    for (const auto value : {target, target - duration}) {
      if (value >= 0 && std::abs(double(value - frame)) < distance) {
        distance = std::abs(double(value - frame));
        best = value;
      }
    }
  };
  consider(0);
  consider(playhead_);
  for (const auto& c : project_.tracks[size_t(track)].clips) {
    if (c.id != drag_clip_) {
      consider(c.position);
      consider(c.position + c.duration);
    }
  }
  return best;
}

void Timeline::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.fillRect(rect(), QColor("#18191b"));
  const auto& project = drag_mode_ ? draft_ : project_;
  const auto visible = visibleRegion().boundingRect();
  const double fps = double(project.rate.numerator) / project.rate.denominator;
  const int step = std::max(1, int(std::ceil(65. / (zoom_ * fps))));
  p.setPen(QColor("#ADBCD1"));
  const int first = std::max(0, int((visible.left() - kHeader) / (zoom_ * fps) / step));
  for (int n = first; kHeader + n * step * zoom_ * fps < visible.right(); ++n) {
    const int x = kHeader + int(n * step * zoom_ * fps);
    p.drawText(x + 4, 22, QString::number(n * step) + "s");
    p.setPen(QColor("#303236"));
    p.drawLine(x, 30, x, height());
    p.setPen(QColor("#ADBCD1"));
  }
  for (int i = 0; i < int(project.tracks.size()); ++i) {
    const auto& track = project.tracks[size_t(i)];
    const int y = kRuler + (int(project.tracks.size()) - i - 1) * kRow;
    p.fillRect(0, y, kHeader, kRow - 1, QColor("#222426"));
    p.setPen(QColor("#E7EDF7"));
    p.drawText(QRect(10, y + 8, kHeader - 16, 24), Qt::AlignLeft | Qt::AlignVCenter,
               QString::fromStdString(track.name));
    QString flags = track.audio_only ? "A" : "V";
    if (track.locked) {
      flags += QStringLiteral(" · 锁定");
    }
    if (track.hidden) {
      flags += QStringLiteral(" · 隐藏");
    }
    if (track.muted) {
      flags += QStringLiteral(" · 静音");
    }
    p.setPen(QColor("#ADBCD1"));
    p.drawText(10, y + 46, flags);
    for (const auto& clip : track.clips) {
      auto r = rect_for(i, clip);
      if (!r.intersects(visible)) {
        continue;
      }
      p.setBrush(QColor(track.audio_only ? "#24645E" : "#234D85"));
      p.setPen(QPen(QColor(selection_.count(clip.id) != 0 ? "#93C5FD" : "#3D659B"),
                    selection_.count(clip.id) != 0 ? 2 : 1));
      p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 5, 5);
      auto a = std::find_if(project.assets.begin(), project.assets.end(),
                            [&](const auto& a) { return a.id == clip.asset_id; });
      p.save();
      p.setClipRect(r.adjusted(3, 3, -3, -3));
      const auto thumbnail = thumbnails_.find(clip.asset_id);
      if (!track.audio_only && thumbnail != thumbnails_.end() && !thumbnail->second.isNull()) {
        const auto& img = thumbnail->second;
        const double w = 64. * img.width() / img.height();
        for (double x = std::max(r.left(), double(visible.left()));
             x < std::min(r.right(), double(visible.right())); x += w) {
          p.drawImage(QRectF(x, r.top(), w, r.height()), img);
        }
      }
      const auto wave = waveforms_.find(clip.asset_id);
      if (wave != waveforms_.end() && !wave->second.empty() && a != project.assets.end()) {
        p.setPen(QColor("#65e1c6"));
        const double total = a->duration_us / 1000000. * fps;
        for (int x = int(std::max(r.left(), double(visible.left())));
             x < std::min(r.right(), double(visible.right())); x += 2) {
          const double source = clip.source_in + (x - r.left()) / zoom_ * clip.speed;
          const size_t index =
              size_t(std::clamp(source / std::max(1., total), 0., .999999) * wave->second.size());
          const double h = wave->second[index] * 16;
          p.drawLine(QPointF(x, r.bottom() - 18 - h), QPointF(x, r.bottom() - 18 + h));
        }
      }
      p.fillRect(QRectF(r.left(), r.top(), r.width(), 20), QColor(0, 0, 0, 160));
      p.restore();
      p.setPen(QColor("#E7EDF7"));
      p.save();
      p.setClipRect(r.adjusted(5, 0, -5, 0));
      p.drawText(r.adjusted(10, 0, -8, 0), Qt::AlignVCenter,
                 !clip.text.content.empty()  ? QString::fromStdString(clip.text.content)
                 : a == project.assets.end() ? QStringLiteral("素材缺失")
                                             : QString::fromStdString(a->name));
      p.restore();
      p.setPen(QColor("#ADBCD1"));
      p.drawLine(r.left() + 4, r.top() + 15, r.left() + 4, r.bottom() - 15);
      p.drawLine(r.right() - 4, r.top() + 15, r.right() - 4, r.bottom() - 15);
    }
  }
  if (project.tracks.empty()) {
    p.setPen(QColor("#ADBCD1"));
    p.drawText(QRect(kHeader + 24, kRuler, 460, 100), Qt::AlignVCenter,
               QStringLiteral("添加视频或音频轨道，再将素材拖入时间线"));
  }
  if (boxing_) {
    p.setBrush(QColor(85, 222, 200, 35));
    p.setPen(QColor("#55dec8"));
    p.drawRect(box_);
  }
  const int x = kHeader + int(playhead_ * zoom_);
  p.setPen(QPen(QColor("#55dec8"), 2));
  p.drawLine(x, 28, x, height());
}

void Timeline::mousePressEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) {
    return;
  }
  press_ = e->position();
  source_track_ = track_at(press_.y());
  if (source_track_ >= 0 && press_.x() < kHeader) {
    if (select) {
      select({}, project_.tracks[size_t(source_track_)].id);
    }
    return;
  }
  if (source_track_ >= 0) {
    const auto& track = project_.tracks[size_t(source_track_)];
    for (const auto& c : track.clips) {
      const auto r = rect_for(source_track_, c);
      if (!r.contains(press_)) {
        continue;
      }
      if (e->modifiers() & Qt::ControlModifier) {
        if (selection_.count(c.id)) {
          selection_.erase(c.id);
        } else {
          selection_.insert(c.id);
        }
        selected_ = c.id;
        if (select) {
          select(c.id, track.id);
        }
        update();
        return;
      }
      if (!selection_.count(c.id)) {
        selection_.clear();
        selection_.insert(c.id);
      }
      selected_ = c.id;
      if (select) {
        select(c.id, track.id);
      }
      if (track.locked) {
        update();
        return;
      }
      drag_clip_ = c.id;
      draft_ = project_;
      drag_mode_ = press_.x() - r.left() < 8 ? 2 : r.right() - press_.x() < 8 ? 3 : 1;
      update();
      return;
    }
  }
  if (source_track_ >= 0 && press_.x() >= kHeader) {
    boxing_ = true;
    box_ = QRectF(press_, press_);
    selection_.clear();
    update();
    return;
  }
  if (press_.x() >= kHeader && seek) {
    scrubbing_ = true;
    seek(frame_at(press_.x()));
  }
}

void Timeline::mouseMoveEvent(QMouseEvent* e) {
  if (boxing_) {
    box_ = QRectF(press_, e->position()).normalized();
    selection_.clear();
    for (size_t t = 0; t < project_.tracks.size(); ++t) {
      for (const auto& c : project_.tracks[t].clips) {
        if (box_.intersects(rect_for(int(t), c))) {
          selection_.insert(c.id);
        }
      }
    }
    update();
    return;
  }
  if (scrubbing_ && seek) {
    seek(frame_at(e->position().x()));
    return;
  }
  if (!drag_mode_) {
    return;
  }
  draft_ = project_;
  auto& from = draft_.tracks[size_t(source_track_)];
  auto it = std::find_if(from.clips.begin(), from.clips.end(),
                         [&](const auto& c) { return c.id == drag_clip_; });
  if (it == from.clips.end()) {
    return;
  }
  const auto delta = int64_t(std::llround((e->position().x() - press_.x()) / zoom_));
  if (drag_mode_ == 1) {
    const int destination = track_at(e->position().y());
    if (destination < 0) {
      return;
    }
    const int64_t shift =
        snap(std::max<int64_t>(0, it->position + delta), it->duration, destination) - it->position;
    const int track_shift = destination - source_track_;
    std::vector<std::pair<int, EditorClip>> moving;
    for (int t = 0; t < int(draft_.tracks.size()); ++t) {
      auto& clips = draft_.tracks[t].clips;
      for (auto iter = clips.begin(); iter != clips.end();) {
        if (!selection_.count(iter->id)) {
          ++iter;
          continue;
        }
        const int target = t + track_shift;
        if (target < 0 || target >= int(draft_.tracks.size()) || iter->position + shift < 0) {
          draft_ = project_;
          update();
          return;
        }
        auto clip = *iter;
        clip.position += shift;
        moving.push_back({target, clip});
        iter = clips.erase(iter);
      }
    }
    for (auto& item : moving) {
      draft_.tracks[item.first].clips.push_back(std::move(item.second));
    }
  } else if (drag_mode_ == 2) {
    const auto offset = std::clamp<int64_t>(
        delta, -std::min(int64_t(std::floor(it->source_in / it->speed)), it->position),
        it->duration - 1);
    it->position += offset;
    it->source_in += int64_t(std::llround(offset * it->speed));
    it->duration -= offset;
    editor_retime(*it, offset);
  } else {
    it->duration = std::max<int64_t>(1, it->duration + delta);
    editor_retime(*it, 0);
  }
  update();
}

void Timeline::mouseReleaseEvent(QMouseEvent* e) {
  scrubbing_ = false;
  if (boxing_) {
    boxing_ = false;
    update();
    return;
  }
  if (!drag_mode_) {
    return;
  }
  const bool moved = (e->position() - press_).manhattanLength() >= 3;
  drag_mode_ = 0;
  drag_clip_.clear();
  if (moved && edit) {
    edit(std::move(draft_));
  }
  update();
}

void Timeline::dragEnterEvent(QDragEnterEvent* e) {
  if (e->mimeData()->hasFormat("application/x-freecut-asset")) {
    e->acceptProposedAction();
  }
}

void Timeline::dragMoveEvent(QDragMoveEvent* e) {
  if (track_at(e->position().y()) >= 0) {
    e->acceptProposedAction();
  }
}

void Timeline::dropEvent(QDropEvent* e) {
  const int track = track_at(e->position().y());
  if (track >= 0 && insert) {
    insert(e->mimeData()->data("application/x-freecut-asset").toStdString(),
           project_.tracks[size_t(track)].id, snap(frame_at(e->position().x()), 0, track));
    e->acceptProposedAction();
  }
}
}  // namespace freecut
