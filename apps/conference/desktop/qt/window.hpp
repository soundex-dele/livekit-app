#pragma once
#include "../session.hpp"
#include "api.hpp"
#include "video_widget.hpp"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QMainWindow>
#include <QPointer>
#include <QTimer>
class QStackedWidget;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QComboBox;
class QGridLayout;
class QTabWidget;
class QCloseEvent;
namespace meeting {
class Window : public QMainWindow {
  Q_OBJECT
public:
  explicit Window(bool running = true);
  void applyBusiness(QJsonObject state);
  void applyMedia(const demo::Snapshot &snapshot);
  void showHome();

protected:
  void closeEvent(QCloseEvent *) override;
  void resizeEvent(QResizeEvent *) override;

private:
  QElapsedTimer elapsed_;
  Api api_;
  demo::Session session_;
  FrameConverter converter_;
  QTimer tick_, poll_, homeTimer_;
  QStackedWidget *pages_;
  QWidget *home_, *waiting_, *meeting_;
  QLabel *notice_, *account_, *meetingTitle_, *waitingText_, *connection_,
      *pageLabel_;
  QListWidget *upcoming_, *history_, *members_;
  QPlainTextEdit *chatLog_, *chatInput_;
  QTabWidget *side_;
  QPushButton *mic_, *camera_, *share_, *manage_;
  QComboBox *layout_;
  QGridLayout *grid_;
  QGridLayout *toolbar_ = nullptr;
  std::vector<QPushButton *> toolbarButtons_;
  std::vector<VideoWidget *> videos_;
  QJsonObject user_, business_;
  QJsonArray meetings_;
  QString meetingId_, serverError_, mediaError_, chatStamp_;
  bool running_, closing_ = false, leaving_ = false, connecting_ = false,
                 polling_ = false, wantCamera_ = false, wantMic_ = false;
  int cameraIndex_ = 0, videoPage_ = 0;
  qint64 lastUnmute_ = 0;
  QString pendingShare_;
  demo::Snapshot media_;
  std::vector<demo::sdk::VideoCaptureDevice> cameras_;
  std::vector<demo::sdk::ScreenCaptureDevice> screens_;
  void login();
  void settings();
  void schedule(QJsonObject existing = {});
  void joinDialog();
  void join(QString number, QString name, QString password);
  void refreshHome();
  void poll();
  void connectMedia();
  void command(QString action, QJsonObject data = {},
               std::function<void()> done = {});
  void leave(bool end = false, bool closeAfter = false);
  void exitMeeting();
  void invite();
  void memberMenu();
  void toggleMicrophone();
  void toggleCamera();
  void toggleShare();
  void updateVideos();
  void refreshDevices();
  void message(QString text);
  QString path(QString suffix = {}) const;
  bool isHost() const;
  void showDetails(QListWidget *list);
};
} // namespace meeting
