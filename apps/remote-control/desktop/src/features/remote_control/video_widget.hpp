#pragma once
#include <QImage>
#include <QKeyEvent>
#include <QPointF>
#include <QWidget>
#include <functional>
#include <livekit/auth.hpp>
#include <optional>
namespace remote_app {
QRectF fit_video(QRectF bounds, QSize source);
std::optional<QPointF> video_point(QRectF bounds, QSize source,
                                   QPointF position);
std::string key_code(const QKeyEvent &event);
class VideoWidget : public QWidget {
public:
  explicit VideoWidget(QWidget *parent = nullptr);
  std::function<void(livekit::sdk::Json)> send;
  std::function<void()> released, target_selected;
  void present(QImage image, bool available, bool busy, uint64_t layout);
  void pause();
  void set_original(bool original);
  bool target() const { return target_; }

protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void wheelEvent(QWheelEvent *) override;
  void keyPressEvent(QKeyEvent *) override;
  void keyReleaseEvent(QKeyEvent *) override;
  void focusOutEvent(QFocusEvent *) override;
  bool event(QEvent *) override;

private:
  std::optional<QPointF> point(QPointF) const;
  void mouse(QMouseEvent *, bool);
  QImage image_;
  bool enabled_ = false, target_ = false, available_ = false, busy_ = false,
       original_ = false;
  uint64_t layout_ = 0;
};
} // namespace remote_app
