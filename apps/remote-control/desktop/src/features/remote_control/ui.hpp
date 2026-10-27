#pragma once
#include "platform/platform.hpp"
#include "services/controller.hpp"
#include "video_widget.hpp"
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QScrollArea>
#include <QSystemTrayIcon>
#include <QTextEdit>
#include <QTimer>
namespace remote_app {
class ControlWindow : public QMainWindow {
public:
  explicit ControlWindow(Controller &, QWidget *parent = nullptr);
  void present(const ControllerSnapshot &);
  std::function<void()> stopped;
  VideoWidget *video() const { return video_; }

protected:
  void closeEvent(QCloseEvent *) override;
  void changeEvent(QEvent *) override;

private:
  Controller &controller_;
  VideoWidget *video_;
  QScrollArea *scroll_;
  QTextEdit *text_;
  QLabel *status_;
  QPushButton *send_;
  uint64_t accepted_ = 0;
  bool finished_ = false;
};
class MainWindow : public QMainWindow {
public:
  explicit MainWindow(bool test = false);
  ~MainWindow();
  void display(const Snapshot &, const ControllerSnapshot &,
               sdk::ControlPermissions);
  void stop_all();

protected:
  void closeEvent(QCloseEvent *) override;

private:
  void refresh();
  void error(const QString &);
  void save();
  bool test_, closing_ = false, regenerating_ = false;
  Session host_;
  Controller controller_;
  std::unique_ptr<Platform> platform_;
  ControlWindow *control_ = nullptr;
  QLineEdit *url_, *name_, *code_, *remote_code_;
  QLabel *status_, *expiry_, *request_, *permissions_, *shortcut_;
  QPushButton *create_, *connect_, *approve_, *reject_, *stop_, *copy_;
  QWidget *pending_;
  QSystemTrayIcon *tray_ = nullptr;
  QTimer timer_;
  QString ui_error_;
  sdk::ControlPermissions permission_cache_;
  sdk::ControlClock::time_point permission_check_{};
};
void apply_theme();
} // namespace remote_app
