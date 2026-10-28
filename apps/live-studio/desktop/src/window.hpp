#pragma once
#include "session.hpp"
#include <QMainWindow>
#include <QWidget>
#include <functional>

namespace studio_app {
// Holds the immutable engine frame for the entire paint; never borrows a worker buffer.
class Preview final : public QWidget {
 public:
  explicit Preview(QWidget* parent = nullptr);
  void setFrame(std::shared_ptr<const sdk::StudioImage> frame);
  void setScene(sdk::StudioScene scene, int selected);
  QRectF canvasRect() const;
  std::function<void(int)> selectSource;
  std::function<void(float, float, bool)> moveSource;
 protected:
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
 private:
  std::shared_ptr<const sdk::StudioImage> frame_;
  sdk::StudioScene scene_;
  int selected_ = -1;
  QPointF last_;
  bool dragging_ = false;
};

struct WindowOptions {
  std::filesystem::path settings;
  bool offline = false;
  bool portrait = false;
};
class Window final : public QMainWindow {
 public:
  explicit Window(WindowOptions options, QWidget* parent = nullptr);
  ~Window() override;
  sdk::StudioSnapshot mediaSnapshot() const;
  bool saveProject();
 protected:
  void closeEvent(QCloseEvent*) override;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
void applyTheme();
void configureOffscreenPlatform();
}
