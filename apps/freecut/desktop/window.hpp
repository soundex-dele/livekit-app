#pragma once
#include "livekit/editor.hpp"
#include "../../player/desktop/audio_output.hpp"
#include <QMainWindow>
#include <deque>
#include <mutex>
#include <map>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTimer;
class QLineEdit;
class QPlainTextEdit;
class QFormLayout;

namespace freecut {
class Timeline;
class Preview;

class Window final : public QMainWindow {
  Q_OBJECT
 public:
  explicit Window(bool silent = false);
  ~Window() override;
  const livekit::sdk::EditorDocument& document() const;
  void import_path(const QString& path);
  bool load_path(const QString& path);
  bool save_path(const QString& path);
  void export_path(const QString& path);
  bool exporting() const;
  bool import_subtitles_path(const QString& path);
  bool export_subtitles_path(const QString& path);
  void seek_frame(int64_t frame);

 signals:
  void imported();
  void export_finished(bool success);

 protected:
  void closeEvent(QCloseEvent*) override;
  void dragEnterEvent(QDragEnterEvent*) override;
  void dropEvent(QDropEvent*) override;

 private:
  template <class T, class Fn>
  void observe(livekit::sdk::AsyncResult<T> result, Fn fn) {
    std::move(result).then(
        [this](auto task) {
          QMetaObject::invokeMethod(this, std::move(task), Qt::QueuedConnection);
        },
        [fn = std::move(fn)](livekit::sdk::AsyncResult<T> ready) mutable { fn(ready.get()); });
  }

  void fail(livekit::sdk::Failure failure);
  void edited(livekit::sdk::Result<void> result);
  void refresh();
  void refresh_properties();
  void select_clip(std::string clip, std::string track);
  void add_track(bool audio);
  void insert_asset(std::string asset, std::string track, int64_t position);
  void insert_selected();
  void remove_selected();
  void apply_properties();
  void relink();
  void load_thumbnails(size_t index, uint64_t epoch);
  void toggle_play();
  void pause();
  void poll();
  void project_settings();
  bool save();
  bool confirm_save();
  void new_project();
  void build_ui();
  void build_effect_properties(QFormLayout* form);
  void add_text();
  void import_subtitles();
  void export_subtitles();
  void copy_clips();
  void paste_clips();
  void filter_assets();
  void update_effect_properties(const livekit::sdk::EditorClip* clip, bool enabled);
  void apply_effect_properties(livekit::sdk::EditorClip& clip);
  void set_transition(const std::string& effect);
  QFormLayout* property_form_ = nullptr;
  std::map<std::string, QDoubleSpinBox*> effects_;
  QPlainTextEdit* text_content_ = nullptr;
  QLineEdit* text_font_ = nullptr;
  QLineEdit* text_color_ = nullptr;
  QComboBox* text_alignment_ = nullptr;
  QComboBox* key_property_ = nullptr;
  QComboBox* key_interpolation_ = nullptr;
  QLineEdit* search_ = nullptr;
  QComboBox* asset_type_ = nullptr;
  QCheckBox* magnetic_ = nullptr;
  std::vector<livekit::sdk::EditorClip> clipboard_;
  std::vector<int> clipboard_tracks_;

  livekit::sdk::EditorDocument document_;
  std::unique_ptr<livekit::sdk::EditorSession> session_;
  std::unique_ptr<livekit::sdk::EditorExport> exporter_;
  player_demo::QtAudioOutput audio_;
  std::mutex mailbox_mutex_;
  std::deque<std::shared_ptr<const livekit::sdk::EditorFrame>> pending_;
  livekit::sdk::Failure pending_error_;
  bool ended_ = false, overflow_ = false;
  bool playing_ = false, silent_ = false, exporting_ = false, updating_ = false;
  bool playback_finished_ = false;
  uint64_t generation_ = 0, document_epoch_ = 0;
  int64_t position_ = 0;
  QString path_;
  std::string selected_clip_, selected_track_;
  QListWidget* assets_ = nullptr;
  Timeline* timeline_ = nullptr;
  Preview* preview_ = nullptr;
  QLabel* notice_ = nullptr;
  QLabel* timecode_ = nullptr;
  QComboBox* tracks_ = nullptr;
  QComboBox* clips_ = nullptr;
  QCheckBox* locked_ = nullptr;
  QCheckBox* hidden_ = nullptr;
  QCheckBox* muted_ = nullptr;
  QSpinBox* position_input_ = nullptr;
  QSpinBox* source_input_ = nullptr;
  QSpinBox* duration_input_ = nullptr;
  QDoubleSpinBox* x_ = nullptr;
  QDoubleSpinBox* y_ = nullptr;
  QDoubleSpinBox* scale_ = nullptr;
  QDoubleSpinBox* opacity_ = nullptr;
  QDoubleSpinBox* volume_ = nullptr;
  QPushButton* play_ = nullptr;
  QPushButton* export_button_ = nullptr;
  QPushButton* cancel_export_ = nullptr;
  QProgressBar* progress_ = nullptr;
  QTimer* poll_timer_ = nullptr;
};
}  // namespace freecut
