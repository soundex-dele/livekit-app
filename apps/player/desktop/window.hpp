#pragma once
#include "controller.hpp"
#include "playlist.hpp"
#include "settings.hpp"
#include "video_widget.hpp"
#include <QElapsedTimer>
#include <QMainWindow>
#include <QSet>
#include <QSlider>
#include <QVector>

class QAction;
class QLabel;
class QListView;
class QMenu;
class QMouseEvent;
class QSplitter;
class QToolButton;

namespace player_demo {
QString formatClock(qint64 microseconds);

// Progress bar with PotPlayer-style click-to-seek and a hover time bubble.
class SeekSlider final : public QSlider {
 public:
  explicit SeekSlider(QWidget* parent = nullptr);

  void setDuration(qint64 microseconds) {
    durationUs_ = microseconds;
  }

 protected:
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;

 private:
  int valueAt(int x) const;
  qint64 durationUs_ = 0;
  bool dragging_ = false;
};

class PlayerWindow final : public QMainWindow {
  Q_OBJECT
 public:
  explicit PlayerWindow(const QString& settingsFile = {}, bool automation = false,
                        std::unique_ptr<AudioOutput> audio = {}, QWidget* parent = nullptr);

  Controller& controller() {
    return controller_;
  }

  PlaylistModel& playlist() {
    return playlist_;
  }

  Settings& settings() {
    return settings_;
  }

  void openFiles(const QStringList& paths, bool append = false);
  void playRow(int row, bool automatic = false);
  void toggleFullscreen();
  void saveState();
  bool saveScreenshot(const QString& path);

  void setAutoAdvance(bool value) {
    autoAdvance_ = value;
  }

 signals:
  void shutdownComplete();

 protected:
  void closeEvent(QCloseEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;

 private:
  QAction* action(const char* name, const QString& text, const QString& icon = {},
                  const QKeySequence& shortcut = {});
  void buildUi();
  void refresh();
  void browse(bool append);
  void browseSubtitles();
  void subtitleSettings();
  void showSettings();
  void showShortcuts();
  void removeSelected();
  void next(bool previous = false);
  void advance(bool failed);
  void remember();
  void revealControls();
  void checkIdle();
  void updateVolume();
  void updateListVisibility();
  void positionOverlays();
  void updateChrome();
  void updateStatus(const sdk::PlayerSnapshot& state);
  void cycleRepeat();
  void adjustVolume(int delta);
  Controller controller_;
  PlaylistModel playlist_;
  Settings settings_;
  bool automation_ = false, autoAdvance_ = true, exiting_ = false, saved_ = false;
  QByteArray normalGeometry_;
  QString pendingSubtitle_, subtitleMedia_;
  int subtitleSize_ = 28;
  int listWidth_ = 0;
  RepeatMode repeat_ = RepeatMode::Sequential;
  QSet<QString> failedIds_;
  VideoWidget* video_ = nullptr;
  QWidget* videoArea_ = nullptr;
  QWidget* controls_ = nullptr;
  QWidget* topBar_ = nullptr;
  QWidget* listPanel_ = nullptr;
  QLabel* topTitle_ = nullptr;
  QLabel* topClock_ = nullptr;
  QLabel* status_ = nullptr;
  QListView* list_ = nullptr;
  SeekSlider* progress_ = nullptr;
  QSlider* volume_ = nullptr;
  QLabel* time_ = nullptr;
  QLabel* banner_ = nullptr;
  QLabel* count_ = nullptr;
  QSplitter* splitter_ = nullptr;
  QMenu* contextMenu_ = nullptr;
  QVector<QAction*> repeatActions_;
  QVector<QToolButton*> optionalButtons_;
  QAction *play_, *stop_, *previous_, *next_, *mute_, *fullscreen_, *listToggle_, *subtitleToggle_;
  QAction* repeatCycle_ = nullptr;
  QElapsedTimer idle_;
  QTimer idleTimer_, historyTimer_;
};
}  // namespace player_demo
