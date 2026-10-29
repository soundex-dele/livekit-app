#include "window.hpp"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace studio_app {
Preview::Preview(QWidget* parent) : QWidget(parent) {
  setObjectName("preview");
  setAccessibleName(QStringLiteral("节目预览，方向键移动素材，Shift 加方向键调整尺寸"));
  setFocusPolicy(Qt::StrongFocus);
  setMinimumSize(280, 200);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void Preview::setFrame(std::shared_ptr<const sdk::StudioImage> frame) {
  frame_ = std::move(frame);
  update();
}

void Preview::setScene(sdk::StudioScene scene, int selected) {
  scene_ = std::move(scene);
  selected_ = selected;
  update();
}

QRectF Preview::canvasRect() const {
  QSizeF size = frame_ ? QSizeF(frame_->width, frame_->height) : QSizeF(1280, 720);
  size.scale(QSizeF(width() - 24, height() - 24), Qt::KeepAspectRatio);
  return {(width() - size.width()) / 2, (height() - size.height()) / 2, size.width(),
          size.height()};
}

void Preview::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor("#090d14"));
  auto canvas = canvasRect();
  if (frame_) {
    QImage image(frame_->rgba.data(), frame_->width, frame_->height, frame_->width * 4,
                 QImage::Format_RGBA8888);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(canvas, image);
  } else {
    painter.setPen(QColor("#b8c2d6"));
    painter.drawText(canvas, Qt::AlignCenter, QStringLiteral("正在准备节目预览…"));
  }
  painter.setClipRect(canvas);
  if (selected_ >= 0 && selected_ < int(scene_.sources.size())) {
    const auto& item = scene_.sources[selected_];
    const auto& t = item.transform;
    QRectF bounds(canvas.x() + t.x * canvas.width(), canvas.y() + t.y * canvas.height(),
                  t.width * canvas.width(), t.height * canvas.height());
    painter.translate(bounds.center());
    painter.rotate(t.rotation);
    bounds.moveCenter({0, 0});
    painter.setPen(QPen(item.locked ? QColor("#b8c2d6") : QColor("#ff668a"), 2));
    painter.drawRect(bounds);
    if (!item.locked)
      painter.fillRect(QRectF(bounds.bottomRight() - QPointF(4, 4), QSizeF(8, 8)),
                       QColor("#ff668a"));
  }
}

void Preview::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton)
    return;
  setFocus();
  auto canvas = canvasRect();
  if (!canvas.contains(event->position()))
    return;
  int hit = -1;
  for (int i = int(scene_.sources.size()) - 1; i >= 0; --i) {
    const auto& item = scene_.sources[i];
    const auto& t = item.transform;
    QRectF bounds(canvas.x() + t.x * canvas.width(), canvas.y() + t.y * canvas.height(),
                  t.width * canvas.width(), t.height * canvas.height());
    QTransform rotation;
    rotation.translate(bounds.center().x(), bounds.center().y())
        .rotate(t.rotation)
        .translate(-bounds.center().x(), -bounds.center().y());
    if (item.visible && bounds.contains(rotation.inverted().map(event->position()))) {
      hit = i;
      break;
    }
  }
  if (selectSource)
    selectSource(hit);
  selected_ = hit;
  dragging_ = hit >= 0 && !scene_.sources[hit].locked;
  last_ = event->position();
  update();
}

void Preview::mouseMoveEvent(QMouseEvent* event) {
  if (!dragging_ || !(event->buttons() & Qt::LeftButton))
    return;
  const auto delta = event->position() - last_;
  last_ = event->position();
  auto canvas = canvasRect();
  if (moveSource)
    moveSource(float(delta.x() / canvas.width()), float(delta.y() / canvas.height()),
               event->modifiers().testFlag(Qt::ShiftModifier));
}

void Preview::mouseReleaseEvent(QMouseEvent*) {
  dragging_ = false;
}

void Preview::keyPressEvent(QKeyEvent* event) {
  if (selected_ < 0 || selected_ >= int(scene_.sources.size()) ||
      scene_.sources[selected_].locked) {
    QWidget::keyPressEvent(event);
    return;
  }
  float x = 0, y = 0;
  switch (event->key()) {
    case Qt::Key_Left:
      x = -.005f;
      break;
    case Qt::Key_Right:
      x = .005f;
      break;
    case Qt::Key_Up:
      y = -.005f;
      break;
    case Qt::Key_Down:
      y = .005f;
      break;
    default:
      QWidget::keyPressEvent(event);
      return;
  }
  if (moveSource)
    moveSource(x, y, event->modifiers().testFlag(Qt::ShiftModifier));
  event->accept();
}
}  // namespace studio_app
