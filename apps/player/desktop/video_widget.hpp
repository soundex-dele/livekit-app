#pragma once
#include <QImage>
#include <QTimer>
#include <QWidget>

class QPainter;

namespace player_demo {
enum class FlashKind { none, play, pause, stop };

class VideoWidget final : public QWidget {
  Q_OBJECT
 public:
  explicit VideoWidget(QWidget* parent = nullptr);

  void setFrame(const QImage& frame) {
    image_ = frame;
    update();
  }

  void setSubtitle(const QString& text, int pixels);
  void setMessage(const QString& text);
  void showOsd(const QString& text);
  void flash(FlashKind kind);

  // Keeps subtitles above the floating control bar.
  void setBottomInset(int pixels) {
    if (bottomInset_ != pixels) {
      bottomInset_ = pixels;
      update();
    }
  }

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
  void paintIdleScreen(QPainter& painter);
  void paintFlash(QPainter& painter);
  QImage image_;
  QString subtitle_, message_, osd_;
  int subtitleSize_ = 28;
  int bottomInset_ = 0;
  FlashKind flash_ = FlashKind::none;
  QTimer osdTimer_, flashTimer_;
};
}  // namespace player_demo
