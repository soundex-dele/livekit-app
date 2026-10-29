#pragma once
#include "domain/snapshot.hpp"
#include <QImage>
#include <QMainWindow>
#include <functional>
class QLineEdit;
class QPushButton;
class QLabel;
class QListWidget;
class QStackedWidget;
class QCloseEvent;

namespace screen_cast {
QString readableError(const std::string& error);
QString normalizedCode(QString text);
bool validCode(const QString& text);
bool validServer(const QString& text);

class Preview final : public QWidget {
 public:
  explicit Preview(QWidget* parent = nullptr);
  void present(QImage image);

 protected:
  void paintEvent(QPaintEvent*) override;

 private:
  QImage image_;
};

class Window final : public QMainWindow {
  Q_OBJECT
 public:
  explicit Window(QWidget* parent = nullptr);
  void setDevices(std::vector<livekit::sdk::ScreenCaptureDevice> devices);
  void present(const Snapshot& snapshot);
  void setPreview(QImage image);
  void setEnumerating(bool busy);
  void showError(const QString& message);
  QString server() const;
  QString code() const;
  livekit::sdk::ScreenCaptureDevice selectedDevice() const;
  void finishClose();
 signals:
  void startRequested();
  void stopRequested();
  void refreshRequested();
  void closeRequested();

 protected:
  void closeEvent(QCloseEvent*) override;

 private:
  void updateControls();
  QLineEdit *code_, *server_;
  QPushButton *start_, *stop_, *refresh_, *save_;
  QLabel *status_, *error_, *serverHint_;
  QListWidget* displays_;
  QStackedWidget* pages_;
  Preview* preview_;
  std::vector<livekit::sdk::ScreenCaptureDevice> devices_;
  bool busy_ = false, enumerating_ = false, closing_ = false, allowClose_ = false;
};
}  // namespace screen_cast
