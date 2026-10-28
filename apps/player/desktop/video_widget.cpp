#include "video_widget.hpp"
#include <QPainter>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QMouseEvent>
#include <algorithm>

namespace player_demo {
VideoWidget::VideoWidget(QWidget* parent) : QWidget(parent) {
  setObjectName("video"); setAcceptDrops(true); setMouseTracking(true);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setMinimumSize(200, 180); setAccessibleName(QStringLiteral("视频画面，可拖入本地文件"));
  osdTimer_.setSingleShot(true); osdTimer_.setInterval(1800);
  connect(&osdTimer_, &QTimer::timeout, this, [this] { osd_.clear(); update(); });
}
void VideoWidget::setSubtitle(const QString& text, int pixels) {
  if (subtitle_ == text && subtitleSize_ == pixels) return;
  subtitle_ = text; subtitleSize_ = pixels; update();
}
void VideoWidget::setMessage(const QString& text) {
  if (message_ == text) return; message_ = text; update();
}
void VideoWidget::showOsd(const QString& text) { osd_ = text; osdTimer_.start(); update(); }
QRect VideoWidget::imageRect() const {
  if (image_.isNull()) return rect();
  const auto size = image_.size().scaled(this->size(), Qt::KeepAspectRatio);
  return QRect(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
}
void VideoWidget::paintEvent(QPaintEvent*) {
  QPainter p(this); p.fillRect(rect(), QColor("#0d0f12"));
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  p.setRenderHint(QPainter::Antialiasing);
  if (!image_.isNull()) p.drawImage(imageRect(), image_);
  else {
    auto font = p.font(); font.setPixelSize(26); p.setFont(font); p.setPen(QColor("#dce3ed"));
    p.drawText(rect().adjusted(30, -35, -30, -35), Qt::AlignCenter, QStringLiteral("本地播放器"));
    font.setPixelSize(14); p.setFont(font); p.setPen(QColor("#9aa8bc"));
    p.drawText(rect().adjusted(30, 45, -30, 45), Qt::AlignCenter | Qt::TextWordWrap,
               message_.isEmpty() ? QStringLiteral("打开文件，或将视频拖到这里") : message_);
  }
  if (!subtitle_.isEmpty()) {
    QFont font = p.font(); font.setPixelSize(std::clamp(subtitleSize_, 16, 64)); font.setBold(true); p.setFont(font);
    const QRect bounds = rect().adjusted(24, 16, -24, -24);
    const int flags = Qt::AlignHCenter | Qt::AlignBottom | Qt::TextWordWrap;
    p.setPen(Qt::black);
    for (int x = -2; x <= 2; ++x) for (int y = -2; y <= 2; ++y)
      if (x || y) p.drawText(bounds.translated(x, y), flags, subtitle_);
    p.setPen(Qt::white); p.drawText(bounds, flags, subtitle_);
  }
  if (!osd_.isEmpty()) {
    QFont font = p.font(); font.setPixelSize(14); font.setBold(false); p.setFont(font);
    const auto box = p.boundingRect(QRect(20, 18, width() - 40, 100), Qt::TextWordWrap, osd_).adjusted(-8, -6, 8, 6);
    p.setPen(Qt::NoPen); p.setBrush(QColor(20, 26, 35, 225)); p.drawRoundedRect(box, 4, 4);
    p.setPen(QColor("#e9f0fa")); p.drawText(box.adjusted(8, 6, -8, -6), Qt::TextWordWrap, osd_);
  }
}
void VideoWidget::dragEnterEvent(QDragEnterEvent* e) {
  for (const auto& url : e->mimeData()->urls()) if (url.isLocalFile()) { e->acceptProposedAction(); return; }
}
void VideoWidget::dropEvent(QDropEvent* e) {
  QStringList paths; for (const auto& url : e->mimeData()->urls()) if (url.isLocalFile()) paths << url.toLocalFile();
  if (!paths.isEmpty()) { e->acceptProposedAction(); emit filesDropped(paths); }
}
void VideoWidget::mouseDoubleClickEvent(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton) emit doubleClicked();
}
}
