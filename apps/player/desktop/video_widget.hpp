#pragma once
#include <QWidget>
#include <QImage>
#include <QTimer>

namespace player_demo {
class VideoWidget final : public QWidget {
  Q_OBJECT
 public:
  explicit VideoWidget(QWidget* parent = nullptr);
  void setFrame(const QImage& frame) { image_ = frame; update(); }
  void setSubtitle(const QString& text, int pixels);
  void setMessage(const QString& text);
  void showOsd(const QString& text);
  QRect imageRect() const;
 signals:
  void filesDropped(const QStringList&);
  void doubleClicked();
 protected:
  void paintEvent(QPaintEvent*) override;
  void dragEnterEvent(QDragEnterEvent*) override;
  void dropEvent(QDropEvent*) override;
  void mouseDoubleClickEvent(QMouseEvent*) override;
 private:
  QImage image_;
  QString subtitle_, message_, osd_;
  int subtitleSize_ = 28;
  QTimer osdTimer_;
};
}
