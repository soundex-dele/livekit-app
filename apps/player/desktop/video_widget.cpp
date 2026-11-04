#include "video_widget.hpp"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygon>
#include <QUrl>
#include <algorithm>

namespace player_demo {
VideoWidget::VideoWidget(QWidget* parent) : QWidget(parent) {
  setObjectName("video");
  setAcceptDrops(true);
  setMouseTracking(true);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setMinimumSize(200, 180);
  setAccessibleName(QStringLiteral("视频画面，可拖入本地文件"));
  osdTimer_.setSingleShot(true);
  osdTimer_.setInterval(1800);
  connect(&osdTimer_, &QTimer::timeout, this, [this] {
    osd_.clear();
    update();
  });
  flashTimer_.setSingleShot(true);
  flashTimer_.setInterval(650);
  connect(&flashTimer_, &QTimer::timeout, this, [this] {
    flash_ = FlashKind::none;
    update();
  });
}

void VideoWidget::setSubtitle(const QString& text, int pixels) {
  if (subtitle_ == text && subtitleSize_ == pixels)
    return;
  subtitle_ = text;
  subtitleSize_ = pixels;
  update();
}

void VideoWidget::setMessage(const QString& text) {
  if (message_ == text)
    return;
  message_ = text;
  update();
}

void VideoWidget::showOsd(const QString& text) {
  osd_ = text;
  osdTimer_.start();
  update();
}

void VideoWidget::flash(FlashKind kind) {
  flash_ = kind;
  flashTimer_.start();
  update();
}

QRect VideoWidget::imageRect() const {
  if (image_.isNull())
    return rect();
  const auto size = image_.size().scaled(this->size(), Qt::KeepAspectRatio);
  return QRect(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
}

void VideoWidget::paintIdleScreen(QPainter& p) {
  const int side = std::clamp(std::min(width(), height()) / 5, 64, 104);
  const QRect logo((width() - side) / 2, (height() - side) / 2 - 44, side, side);
  p.setPen(QPen(QColor("#2b3a4d"), 2));
  p.setBrush(QColor(18, 24, 33, 210));
  p.drawEllipse(logo);
  const int extent = side / 3;
  const QPoint center = logo.center();
  QPolygon triangle;
  triangle << QPoint(center.x() - extent / 2 + 3, center.y() - extent / 2)
           << QPoint(center.x() - extent / 2 + 3, center.y() + extent / 2)
           << QPoint(center.x() + extent / 2 + 3, center.y());
  p.setPen(Qt::NoPen);
  p.setBrush(QColor("#7fb4f5"));
  p.drawPolygon(triangle);
  QFont font = p.font();
  font.setPixelSize(15);
  p.setFont(font);
  p.setPen(QColor("#c8d6e8"));
  const QString hint = message_.isEmpty() ? QStringLiteral("打开文件，或将视频拖到这里") : message_;
  p.drawText(QRect(20, logo.bottom() + 22, std::max(1, width() - 40), 44),
             Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, hint);
  font.setPixelSize(12);
  p.setFont(font);
  p.setPen(QColor("#718096"));
  p.drawText(QRect(20, logo.bottom() + 60, std::max(1, width() - 40), 28),
             Qt::AlignHCenter | Qt::AlignTop,
             QStringLiteral("Ctrl+O 打开媒体文件  ·  也可直接拖入"));
}

void VideoWidget::paintFlash(QPainter& p) {
  if (flash_ == FlashKind::none)
    return;
  const int side = std::clamp(std::min(width(), height()) / 6, 64, 112);
  const QRect box((width() - side) / 2, (height() - side) / 2, side, side);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(8, 11, 16, 180));
  p.drawRoundedRect(box, 12, 12);
  p.setBrush(QColor(232, 241, 255, 225));
  const QRect inner = box.adjusted(side / 3, side / 3, -side / 3, -side / 3);
  if (flash_ == FlashKind::play) {
    QPolygon triangle;
    triangle << QPoint(inner.left(), inner.top()) << QPoint(inner.left(), inner.bottom())
             << QPoint(inner.right(), inner.center().y());
    p.drawPolygon(triangle);
  } else if (flash_ == FlashKind::pause) {
    const int bar = std::max(4, inner.width() / 3);
    p.drawRoundedRect(QRect(inner.left(), inner.top(), bar, inner.height()), 2, 2);
    p.drawRoundedRect(QRect(inner.right() - bar + 1, inner.top(), bar, inner.height()), 2, 2);
  } else {
    p.drawRoundedRect(inner, 3, 3);
  }
}

void VideoWidget::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), QColor("#080a0d"));
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  p.setRenderHint(QPainter::Antialiasing);
  if (!image_.isNull())
    p.drawImage(imageRect(), image_);
  else
    paintIdleScreen(p);
  if (!subtitle_.isEmpty()) {
    QFont font = p.font();
    font.setPixelSize(std::clamp(subtitleSize_, 16, 64));
    font.setBold(true);
    p.setFont(font);
    const QRect bounds = rect().adjusted(24, 16, -24, -bottomInset_ - 16);
    const int flags = Qt::AlignHCenter | Qt::AlignBottom | Qt::TextWordWrap;
    p.setPen(Qt::black);
    for (int x = -2; x <= 2; ++x)
      for (int y = -2; y <= 2; ++y)
        if (x || y)
          p.drawText(bounds.translated(x, y), flags, subtitle_);
    p.setPen(Qt::white);
    p.drawText(bounds, flags, subtitle_);
  }
  paintFlash(p);
  if (!osd_.isEmpty()) {
    QFont font = p.font();
    font.setPixelSize(15);
    font.setBold(true);
    p.setFont(font);
    const auto box = p.boundingRect(QRect(18, 16, std::max(1, width() - 36), 120),
                                    Qt::TextWordWrap, osd_)
                         .adjusted(-10, -7, 10, 7);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(8, 12, 18, 210));
    p.drawRoundedRect(box, 5, 5);
    p.setPen(QColor("#eef4ff"));
    p.drawText(box.adjusted(10, 7, -10, -7), Qt::TextWordWrap, osd_);
  }
}

void VideoWidget::dragEnterEvent(QDragEnterEvent* e) {
  for (const auto& url : e->mimeData()->urls())
    if (url.isLocalFile()) {
      e->acceptProposedAction();
      return;
    }
}

void VideoWidget::dropEvent(QDropEvent* e) {
  QStringList paths;
  for (const auto& url : e->mimeData()->urls())
    if (url.isLocalFile())
      paths << url.toLocalFile();
  if (!paths.isEmpty()) {
    e->acceptProposedAction();
    emit filesDropped(paths);
  }
}

void VideoWidget::mouseDoubleClickEvent(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton)
    emit doubleClicked();
}
}  // namespace player_demo
