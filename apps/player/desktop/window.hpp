#pragma once
#include "controller.hpp"
#include "playlist.hpp"
#include "settings.hpp"
#include "video_widget.hpp"
#include <QMainWindow>
#include <QSet>
#include <QElapsedTimer>

class QAction;
class QLabel;
class QListView;
class QSlider;
class QSplitter;
class QMenu;
namespace player_demo {
class PlayerWindow final : public QMainWindow {
  Q_OBJECT
 public:
  explicit PlayerWindow(const QString& settingsFile = {}, bool automation = false,
                        std::unique_ptr<AudioOutput> audio = {}, QWidget* parent = nullptr);
  Controller& controller() { return controller_; }
  PlaylistModel& playlist() { return playlist_; }
  Settings& settings() { return settings_; }
  void openFiles(const QStringList& paths, bool append = false);
  void playRow(int row, bool automatic = false);
  void toggleFullscreen();
  void saveState();
  bool saveScreenshot(const QString& path);
  void setAutoAdvance(bool value) { autoAdvance_ = value; }
 signals:
  void shutdownComplete();
 protected:
  void closeEvent(QCloseEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;
 private:
  QAction* action(const char* name, const QString& text, const QString& icon = {}, const QKeySequence& shortcut = {});
  void buildUi();
  void refresh();
  void browse(bool append);
  void browseSubtitles();
  void subtitleSettings();
  void showSettings();
  void removeSelected();
  void next(bool previous = false);
  void advance(bool failed);
  void remember();
  void revealControls();
  void checkIdle();
  void updateVolume();
  void updateListVisibility();
  Controller controller_;
  PlaylistModel playlist_;
  Settings settings_;
  bool automation_ = false, autoAdvance_ = true, exiting_ = false, saved_ = false;
  QByteArray normalGeometry_;
  QString pendingSubtitle_, subtitleMedia_;
  int subtitleSize_ = 28;
  RepeatMode repeat_ = RepeatMode::Sequential;
  QSet<QString> failedIds_;
  VideoWidget* video_ = nullptr;
  QWidget* controls_ = nullptr;
  QWidget* listPanel_ = nullptr;
  QListView* list_ = nullptr;
  QSlider* progress_ = nullptr;
  QSlider* volume_ = nullptr;
  QLabel* time_ = nullptr;
  QLabel* banner_ = nullptr;
  QLabel* count_ = nullptr;
  QSplitter* splitter_ = nullptr;
  QMenu* contextMenu_ = nullptr;
  QAction *play_, *stop_, *previous_, *next_, *mute_, *fullscreen_, *listToggle_, *subtitleToggle_;
  QElapsedTimer idle_;
  QTimer idleTimer_, historyTimer_;
};
}
