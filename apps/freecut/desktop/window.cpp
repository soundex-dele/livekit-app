#include "window.hpp"
#include "timeline.hpp"
#include <av/ui.hpp>
#include <QAction>
#include <QLineEdit>
#include <QSettings>
#include <QTabWidget>
#include <QSignalBlocker>
#include <cmath>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>

namespace freecut {
using namespace livekit::sdk;

namespace {
std::string id() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

QString text(const std::string& s) {
  return QString::fromStdString(s);
}

std::filesystem::path file_path(const QString& s) {
  return std::filesystem::u8path(s.toUtf8().toStdString());
}

class AssetList final : public QListWidget {
 public:
  explicit AssetList(QWidget* parent) : QListWidget(parent) {
    setDragEnabled(true);
    setIconSize({96, 54});
    setSpacing(8);
    setViewMode(QListView::IconMode);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setGridSize({120, 92});
    setWordWrap(true);
  }

 protected:
  QStringList mimeTypes() const override {
    return {"application/x-freecut-asset"};
  }

  QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override {
    auto* data = new QMimeData;
    if (!items.isEmpty()) {
      data->setData("application/x-freecut-asset",
                    items.first()->data(Qt::UserRole).toString().toUtf8());
    }
    return data;
  }
};

QImage image(const EditorImage& input) {
  if (input.rgba.empty()) {
    return {};
  }
  return QImage(input.rgba.data(), input.width, input.height, input.width * 4,
                QImage::Format_RGBA8888)
      .copy();
}
}  // namespace

class Preview final : public QWidget {
 public:
  explicit Preview(QWidget* parent) : QWidget(parent) {
    setObjectName("preview");
    setMinimumSize(320, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName(QStringLiteral("剪辑画面预览"));
  }

  void set_image(const EditorImage& frame) {
    image_ = image(frame);
    setProperty("hasFrame", !image_.isNull());
    update();
  }

  std::function<void(double, double, double)> transform;
  bool editable = false;

  void clear_image() {
    image_ = {};
    setProperty("hasFrame", false);
    update();
  }

 protected:
  void mousePressEvent(QMouseEvent* event) override {
    if (!editable || event->button() != Qt::LeftButton) {
      return;
    }
    press_ = event->position();
    dragging_ = true;
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (!dragging_) {
      return;
    }
    dragging_ = false;
    if (!image_.isNull() && transform) {
      QSize size = image_.size();
      size.scale(this->size(), Qt::KeepAspectRatio);
      const auto delta = event->position() - press_;
      if (delta.manhattanLength() >= 3) {
        transform(delta.x() / size.width(), delta.y() / size.height(), 1);
      }
    }
  }

  void wheelEvent(QWheelEvent* event) override {
    if (editable && transform && event->modifiers() & Qt::ControlModifier) {
      transform(0, 0, event->angleDelta().y() > 0 ? 1.05 : 1 / 1.05);
      event->accept();
    } else {
      event->ignore();
    }
  }

  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.fillRect(rect(), QColor("#050912"));
    if (image_.isNull()) {
      p.setPen(QColor("#ADBCD1"));
      p.drawText(rect(), Qt::AlignCenter, QStringLiteral("导入素材，开始剪辑"));
      return;
    }
    auto size = image_.size();
    size.scale(this->size(), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
    p.drawImage(target, image_);
  }

 private:
  QImage image_;
  QPointF press_;
  bool dragging_ = false;
};

Window::Window(bool silent) : silent_(silent) {
  setObjectName("freecutWindow");
  setAcceptDrops(true);
  resize(1440, 900);
  setMinimumSize(1000, 680);
  av::ui::apply_theme(this, av::ui::Theme::Dark);
  build_ui();
  setStyleSheet(
      styleSheet() +
      QStringLiteral(
          "QMainWindow, QWidget#freecutWindow { background: #151617; color: #e5e7eb; }"
          "QSplitter::handle { background: #101112; width: 5px; height: 5px; }"
          "QListWidget, QScrollArea, QPlainTextEdit { background: #1d1e20; border: 1px solid "
          "#303236; }"
          "QTabWidget::pane { border: 0; } QTabBar::tab { padding: 10px 14px; background: #1d1e20; "
          "}"
          "QTabBar::tab:selected { color: #55dec8; border-bottom: 2px solid #55dec8; }"
          "QPushButton { border-radius: 5px; padding: 6px 10px; }"
          "QPushButton#exportMp4 { background: #56dec9; color: #10211f; font-weight: bold; }"
          "QLineEdit:focus, QDoubleSpinBox:focus, QSpinBox:focus { border: 1px solid #55dec8; }"));
  session_ = std::make_unique<EditorSession>(EditorCallbacks{
      [this](auto frame) {
        std::lock_guard lock(mailbox_mutex_);
        const size_t frame_bytes = frame->image.rgba.size() + frame->audio.size() * sizeof(float);
        size_t bytes = 0;
        for (const auto& item : pending_) {
          bytes += item->image.rgba.size() + item->audio.size() * sizeof(float);
        }
        while (!pending_.empty() &&
               (pending_.size() >= 8 || bytes + frame_bytes > 128 * 1024 * 1024)) {
          bytes -=
              pending_.front()->image.rgba.size() + pending_.front()->audio.size() * sizeof(float);
          pending_.pop_front();
          overflow_ = true;
        }
        pending_.push_back(std::move(frame));
      },
      [this](Failure error) {
        std::lock_guard lock(mailbox_mutex_);
        pending_error_ = std::move(error);
      },
      [this] {
        std::lock_guard lock(mailbox_mutex_);
        ended_ = true;
      }});
  exporter_ = std::make_unique<EditorExport>();
  new_project();
  poll_timer_ = new QTimer(this);
  poll_timer_->setInterval(8);
  connect(poll_timer_, &QTimer::timeout, this, &Window::poll);
  poll_timer_->start();
}

Window::~Window() {
  if (!silent_) {
    QSettings settings("FreeCut", "Editor");
    for (auto* splitter : findChildren<QSplitter*>()) {
      settings.setValue(splitter->objectName(), splitter->saveState());
    }
  }
  poll_timer_->stop();
  exporter_->cancel();
  exporter_.reset();
  session_.reset();
  audio_.close();
}

const EditorDocument& Window::document() const {
  return document_;
}

bool Window::exporting() const {
  return exporting_;
}

void Window::build_ui() {
  auto* file = menuBar()->addMenu(QStringLiteral("文件"));
  auto action = [&](QMenu* menu, const QString& name, const QKeySequence& key, auto fn) {
    auto* a = menu->addAction(name);
    a->setShortcut(key);
    connect(a, &QAction::triggered, this, fn);
    return a;
  };
  action(file, QStringLiteral("新建工程"), QKeySequence::New, [this] {
    if (confirm_save()) {
      new_project();
    }
  });
  action(file, QStringLiteral("打开工程…"), QKeySequence::Open, [this] {
    if (!confirm_save()) {
      return;
    }
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("打开工程"), {},
                                                   "FreeCut (*.freecut.json)");
    if (!path.isEmpty()) {
      load_path(path);
    }
  });
  action(file, QStringLiteral("保存工程"), QKeySequence::Save, [this] { save(); });
  action(file, QStringLiteral("另存为…"), QKeySequence::SaveAs, [this] {
    const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("保存工程"), path_,
                                                   "FreeCut (*.freecut.json)");
    if (!path.isEmpty()) {
      save_path(path.endsWith(".freecut.json") ? path : path + ".freecut.json");
    }
  });
  auto import = [this] {
    const auto paths = QFileDialog::getOpenFileNames(
        this, QStringLiteral("导入媒体"), {},
        QStringLiteral("媒体 (*.mp4 *.mov *.mkv *.webm *.avi *.mp3 *.wav *.flac *.m4a *.aac *.png "
                       "*.jpg *.jpeg *.bmp *.webp);;所有文件 (*)"));
    for (const auto& path : paths) {
      import_path(path);
    }
  };
  action(file, QStringLiteral("导入素材…"), QKeySequence("Ctrl+I"), import);
  action(file, QStringLiteral("工程设置…"), {}, [this] { project_settings(); });
  auto* edit = menuBar()->addMenu(QStringLiteral("编辑"));
  action(edit, QStringLiteral("撤销"), QKeySequence::Undo, [this] {
    if (document_.undo()) {
      refresh();
    }
  });
  action(edit, QStringLiteral("重做"), QKeySequence::Redo, [this] {
    if (document_.redo()) {
      refresh();
    }
  });
  action(edit, QStringLiteral("复制片段"), QKeySequence::Copy, [this] { copy_clips(); });
  action(edit, QStringLiteral("粘贴片段"), QKeySequence::Paste, [this] { paste_clips(); });
  action(edit, QStringLiteral("分割片段"), QKeySequence("Ctrl+B"),
         [this] { edited(document_.split(selected_clip_, position_, id())); });
  action(edit, QStringLiteral("删除片段"), QKeySequence::Delete, [this] { remove_selected(); });
  action(edit, QStringLiteral("插入所选素材"), QKeySequence("Ctrl+Return"),
         [this] { insert_selected(); });
  action(edit, QStringLiteral("前一帧"), QKeySequence(Qt::Key_Left),
         [this] { seek_frame(std::max<int64_t>(0, position_ - 1)); });
  action(edit, QStringLiteral("后一帧"), QKeySequence(Qt::Key_Right),
         [this] { seek_frame(position_ + 1); });
  action(edit, QStringLiteral("播放 / 暂停"), QKeySequence(Qt::Key_Space),
         [this] { toggle_play(); });

  auto* root = new QWidget(this);
  auto* layout = new QVBoxLayout(root);
  layout->setContentsMargins(16, 12, 16, 12);
  layout->setSpacing(12);
  auto* toolbar = new QHBoxLayout;
  auto button = [&](const QString& label, const char* name, auto fn) {
    auto* b = new QPushButton(label, this);
    b->setObjectName(name);
    b->setAccessibleName(label);
    connect(b, &QPushButton::clicked, this, fn);
    toolbar->addWidget(b);
    return b;
  };
  button(QStringLiteral("导入素材"), "importMedia", import);
  button(QStringLiteral("添加视频轨"), "addVideoTrack", [this] { add_track(false); });
  button(QStringLiteral("添加音频轨"), "addAudioTrack", [this] { add_track(true); });
  button(QStringLiteral("分割"), "splitClip",
         [this] { edited(document_.split(selected_clip_, position_, id())); });
  auto* snap = new QCheckBox(QStringLiteral("吸附"), this);
  snap->setChecked(true);
  toolbar->addWidget(snap);
  auto* zoom = new QSpinBox(this);
  zoom->setRange(10, 2000);
  zoom->setValue(300);
  zoom->setSuffix("%");
  zoom->setObjectName("timelineZoom");
  toolbar->addWidget(new QLabel(QStringLiteral("时间线缩放"), this));
  toolbar->addWidget(zoom);
  magnetic_ = new QCheckBox(QStringLiteral("主轨磁吸"), this);
  magnetic_->setObjectName("mainMagnetic");
  toolbar->addWidget(magnetic_);
  connect(magnetic_, &QCheckBox::clicked, this, [this](bool on) {
    auto p = document_.project();
    p.magnetic = on;
    edited(document_.edit(std::move(p)));
  });
  button(QStringLiteral("适配"), "fitTimeline", [this, zoom] {
    zoom->setValue(
        int(100. * std::max(0.1, (width() - 180.) /
                                     std::max<int64_t>(1, editor_duration(document_.project())))));
  });
  toolbar->addStretch();
  export_button_ = button(QStringLiteral("导出 MP4"), "exportMp4", [this] {
    const auto path =
        QFileDialog::getSaveFileName(this, QStringLiteral("导出 MP4"), {}, "MP4 (*.mp4)");
    if (!path.isEmpty()) {
      export_path(path.endsWith(".mp4", Qt::CaseInsensitive) ? path : path + ".mp4");
    }
  });
  av::ui::set_button_role(export_button_, av::ui::ButtonRole::Primary);
  layout->addLayout(toolbar);
  auto* vertical = new QSplitter(Qt::Vertical, root);
  vertical->setObjectName("verticalLayout");
  auto* workspace = new QSplitter(Qt::Horizontal, vertical);
  workspace->setObjectName("workspaceLayout");
  auto* library = new QWidget(workspace);
  auto* lib = new QVBoxLayout(library);
  lib->setContentsMargins(0, 0, 8, 0);
  auto* heading = new QLabel(QStringLiteral("FreeCut  /  创作工作台"), library);
  av::ui::set_text_role(heading, "section");
  lib->addWidget(heading);
  auto* tabs = new QTabWidget(library);
  tabs->setObjectName("libraryTabs");
  lib->addWidget(tabs, 1);
  auto* media = new QWidget(tabs);
  auto* media_layout = new QVBoxLayout(media);
  search_ = new QLineEdit(media);
  search_->setPlaceholderText(QStringLiteral("搜索素材"));
  search_->setObjectName("assetSearch");
  search_->setAccessibleName(QStringLiteral("搜索素材"));
  media_layout->addWidget(search_);
  asset_type_ = new QComboBox(media);
  asset_type_->addItems({QStringLiteral("全部"), QStringLiteral("视频"), QStringLiteral("音频"),
                         QStringLiteral("图片")});
  media_layout->addWidget(asset_type_);
  connect(search_, &QLineEdit::textChanged, this, &Window::filter_assets);
  connect(asset_type_, &QComboBox::currentIndexChanged, this, &Window::filter_assets);
  tabs->addTab(media, QStringLiteral("媒体"));
  auto page = [&](const QString& title) {
    auto* widget = new QWidget(tabs);
    auto* box = new QVBoxLayout(widget);
    tabs->addTab(widget, title);
    return box;
  };
  auto entry = [&](QVBoxLayout* box, const QString& title, auto fn) {
    auto* b = new QPushButton(title, this);
    box->addWidget(b);
    connect(b, &QPushButton::clicked, this, fn);
  };
  auto* audio_page = page(QStringLiteral("音频"));
  entry(audio_page, QStringLiteral("导入音频…"), import);
  entry(audio_page, QStringLiteral("查看音频素材"), [this, tabs] {
    asset_type_->setCurrentIndex(2);
    tabs->setCurrentIndex(0);
  });
  audio_page->addWidget(new QLabel(QStringLiteral("选择片段后，在右侧调整音量与淡入淡出。"), this));
  audio_page->addStretch();
  auto* text_page = page(QStringLiteral("文本"));
  entry(text_page, QStringLiteral("添加文本"), [this] { add_text(); });
  entry(text_page, QStringLiteral("导入 SRT 字幕…"), [this] { import_subtitles(); });
  entry(text_page, QStringLiteral("导出 SRT 字幕…"), [this] { export_subtitles(); });
  text_page->addStretch();
  auto* transitions = page(QStringLiteral("转场"));
  entry(transitions, QStringLiteral("叠化"), [this] { set_transition("fade"); });
  entry(transitions, QStringLiteral("擦除"), [this] { set_transition("wipe"); });
  entry(transitions, QStringLiteral("推拉"), [this] { set_transition("slide"); });
  entry(transitions, QStringLiteral("移除转场"), [this] { set_transition(""); });
  transitions->addStretch();
  auto* colors = page(QStringLiteral("调色"));
  colors->addWidget(
      new QLabel(QStringLiteral("选择视频或图片，在右侧调整亮度、对比度、饱和度与色温。"), this));
  entry(colors, QStringLiteral("重置调色"), [this] {
    auto p = document_.project();
    for (auto& t : p.tracks) {
      for (auto& c : t.clips) {
        if (c.id == selected_clip_) {
          c.color = {};
        }
      }
    }
    edited(document_.edit(std::move(p)));
  });
  colors->addStretch();
  assets_ = new AssetList(media);
  assets_->setObjectName("assetList");
  media_layout->addWidget(assets_);
  auto* add = new QPushButton(QStringLiteral("插入到播放头"), library);
  add->setObjectName("insertAsset");
  lib->addWidget(add);
  connect(add, &QPushButton::clicked, this, &Window::insert_selected);
  connect(assets_, &QListWidget::itemDoubleClicked, this, [this] { insert_selected(); });
  auto* relink_button = new QPushButton(QStringLiteral("重新定位所选素材…"), library);
  lib->addWidget(relink_button);
  connect(relink_button, &QPushButton::clicked, this, &Window::relink);
  auto* center = new QWidget(workspace);
  auto* preview_layout = new QVBoxLayout(center);
  preview_layout->setContentsMargins(4, 0, 4, 0);
  auto* player_header = new QHBoxLayout;
  player_header->addWidget(new QLabel(QStringLiteral("播放器"), center));
  player_header->addStretch();
  auto* ratio = new QComboBox(center);
  ratio->setObjectName("canvasRatio");
  ratio->addItems({QStringLiteral("画布比例"), "16:9", "9:16", "1:1", "4:3"});
  connect(ratio, &QComboBox::activated, this, [this](int index) {
    if (!index) {
      return;
    }
    auto p = document_.project();
    const std::pair<int, int> sizes[] = {{1920, 1080}, {1080, 1920}, {1080, 1080}, {1440, 1080}};
    p.width = sizes[index - 1].first;
    p.height = sizes[index - 1].second;
    edited(document_.edit(std::move(p)));
  });
  player_header->addWidget(ratio);
  preview_layout->addLayout(player_header);
  preview_ = new Preview(center);
  preview_->setToolTip(QStringLiteral("拖动调整所选片段位置；Ctrl + 滚轮缩放"));
  preview_->transform = [this](double dx, double dy, double scale) {
    auto p = document_.project();
    for (auto& t : p.tracks) {
      for (auto& c : t.clips) {
        if (c.id == selected_clip_) {
          c.x = std::clamp(c.x + dx, -4., 4.);
          c.y = std::clamp(c.y + dy, -4., 4.);
          c.scale = std::clamp(c.scale * scale, .01, 8.);
        }
      }
    }
    edited(document_.edit(std::move(p)));
  };
  preview_layout->addWidget(preview_, 1);
  auto* transport = new QHBoxLayout;
  play_ = new QPushButton(QStringLiteral("播放"), center);
  play_->setObjectName("playPause");
  transport->addWidget(play_);
  connect(play_, &QPushButton::clicked, this, &Window::toggle_play);
  auto* prev = new QPushButton(QStringLiteral("前一帧"), center);
  transport->addWidget(prev);
  connect(prev, &QPushButton::clicked, this,
          [this] { seek_frame(std::max<int64_t>(0, position_ - 1)); });
  auto* next = new QPushButton(QStringLiteral("后一帧"), center);
  transport->addWidget(next);
  connect(next, &QPushButton::clicked, this, [this] { seek_frame(position_ + 1); });
  timecode_ = new QLabel(center);
  timecode_->setObjectName("timecode");
  transport->addStretch();
  transport->addWidget(timecode_);
  preview_layout->addLayout(transport);
  auto* properties_scroll = new QScrollArea(workspace);
  properties_scroll->setWidgetResizable(true);
  properties_scroll->setMinimumWidth(240);
  auto* properties = new QWidget(properties_scroll);
  auto* form = new QFormLayout(properties);
  av::ui::set_surface(properties, "sidebar");
  form->setContentsMargins(12, 12, 12, 12);
  form->setSpacing(8);
  form->setRowWrapPolicy(QFormLayout::WrapAllRows);
  clips_ = new QComboBox(properties);
  clips_->setObjectName("selectedClip");
  form->addRow(QStringLiteral("片段"), clips_);
  connect(clips_, &QComboBox::activated, this, [this](int index) {
    const auto clip = clips_->itemData(index).toString().toStdString();
    for (const auto& track : document_.project().tracks) {
      for (const auto& c : track.clips) {
        if (c.id == clip) {
          select_clip(clip, track.id);
          timeline_->set_selected(clip);
          return;
        }
      }
    }
  });
  tracks_ = new QComboBox(properties);
  tracks_->setObjectName("destinationTrack");
  form->addRow(QStringLiteral("轨道 / 移至"), tracks_);
  connect(tracks_, &QComboBox::activated, this, [this](int index) {
    if (selected_clip_.empty()) {
      selected_track_ = tracks_->itemData(index).toString().toStdString();
      refresh_properties();
    }
  });
  locked_ = new QCheckBox(QStringLiteral("锁定"), properties);
  hidden_ = new QCheckBox(QStringLiteral("隐藏画面"), properties);
  muted_ = new QCheckBox(QStringLiteral("静音"), properties);
  locked_->setObjectName("trackLocked");
  hidden_->setObjectName("trackHidden");
  muted_->setObjectName("trackMuted");
  form->addRow(locked_);
  form->addRow(hidden_);
  form->addRow(muted_);
  auto track_toggle = [this] {
    if (updating_) {
      return;
    }
    auto p = document_.project();
    for (auto& t : p.tracks) {
      if (t.id == selected_track_) {
        t.locked = locked_->isChecked();
        t.hidden = hidden_->isChecked();
        t.muted = muted_->isChecked();
      }
    }
    edited(document_.edit(std::move(p)));
  };
  connect(locked_, &QCheckBox::clicked, this, track_toggle);
  connect(hidden_, &QCheckBox::clicked, this, track_toggle);
  connect(muted_, &QCheckBox::clicked, this, track_toggle);
  auto* delete_track = new QPushButton(QStringLiteral("删除所选轨道"), properties);
  form->addRow(delete_track);
  connect(delete_track, &QPushButton::clicked, this, [this] {
    auto p = document_.project();
    if (p.main_track_id == selected_track_) {
      p.main_track_id.clear();
      p.magnetic = false;
    }
    p.tracks.erase(std::remove_if(p.tracks.begin(), p.tracks.end(),
                                  [&](const auto& t) { return t.id == selected_track_; }),
                   p.tracks.end());
    edited(document_.edit(std::move(p)));
  });
  auto spin = [&](const QString& label, const char* name, int minimum) {
    auto* s = new QSpinBox(properties);
    s->setRange(minimum, 100000000);
    s->setObjectName(name);
    form->addRow(label, s);
    return s;
  };
  position_input_ = spin(QStringLiteral("起始帧"), "clipPosition", 0);
  source_input_ = spin(QStringLiteral("源入点（帧）"), "clipSourceIn", 0);
  duration_input_ = spin(QStringLiteral("时长（帧）"), "clipDuration", 1);
  auto number = [&](const QString& label, const char* name, double min, double max, double value) {
    auto* s = new QDoubleSpinBox(properties);
    s->setRange(min, max);
    s->setDecimals(3);
    s->setSingleStep(.05);
    s->setValue(value);
    s->setObjectName(name);
    form->addRow(label, s);
    return s;
  };
  x_ = number(QStringLiteral("水平偏移（画布比例）"), "clipX", -4, 4, 0);
  y_ = number(QStringLiteral("垂直偏移（画布比例）"), "clipY", -4, 4, 0);
  scale_ = number(QStringLiteral("缩放"), "clipScale", .01, 8, 1);
  opacity_ = number(QStringLiteral("透明度"), "clipOpacity", 0, 1, 1);
  volume_ = number(QStringLiteral("音量"), "clipVolume", 0, 4, 1);
  build_effect_properties(form);
  auto* apply = new QPushButton(QStringLiteral("应用片段属性"), properties);
  apply->setObjectName("applyProperties");
  form->addRow(apply);
  connect(apply, &QPushButton::clicked, this, &Window::apply_properties);
  properties_scroll->setWidget(properties);
  workspace->setSizes({400, 700, 280});
  auto* timeline_scroll = new QScrollArea(vertical);
  timeline_scroll->setWidgetResizable(true);
  timeline_ = new Timeline(timeline_scroll);
  timeline_scroll->setWidget(timeline_);
  timeline_->seek = [this](auto frame) { seek_frame(frame); };
  timeline_->select = [this](auto clip, auto track) {
    select_clip(std::move(clip), std::move(track));
  };
  timeline_->edit = [this](auto project) { edited(document_.edit(std::move(project))); };
  timeline_->insert = [this](auto asset, auto track, auto frame) {
    insert_asset(std::move(asset), std::move(track), frame);
  };
  connect(snap, &QCheckBox::toggled, this, [this](bool on) { timeline_->snapping = on; });
  connect(zoom, &QSpinBox::valueChanged, this,
          [this](int value) { timeline_->set_zoom(value / 100.); });
  vertical->setSizes({520, 260});
  layout->addWidget(vertical, 1);
  auto* export_row = new QHBoxLayout;
  progress_ = new QProgressBar(root);
  progress_->setObjectName("exportProgress");
  progress_->setRange(0, 1000);
  progress_->hide();
  export_row->addWidget(progress_, 1);
  cancel_export_ = new QPushButton(QStringLiteral("取消导出"), root);
  cancel_export_->setObjectName("cancelExport");
  cancel_export_->hide();
  export_row->addWidget(cancel_export_);
  connect(cancel_export_, &QPushButton::clicked, this, [this] { exporter_->cancel(); });
  layout->addLayout(export_row);
  notice_ = new QLabel(QStringLiteral("就绪"), root);
  notice_->setObjectName("notice");
  notice_->setWordWrap(true);
  notice_->setTextFormat(Qt::PlainText);
  layout->addWidget(notice_);
  setCentralWidget(root);
  if (!silent_) {
    QSettings settings("FreeCut", "Editor");
    for (auto* splitter : findChildren<QSplitter*>()) {
      splitter->restoreState(settings.value(splitter->objectName()).toByteArray());
    }
  }
}

void Window::fail(Failure error) {
  if (!error || error->code == ErrorCode::cancelled) {
    return;
  }
  notice_->setText(QStringLiteral("操作失败：") + text(error->message));
  av::ui::set_tone(notice_, av::ui::Tone::Error);
}

void Window::pause() {
  playback_finished_ = false;
  const bool was_playing = playing_;
  playing_ = false;
  play_->setText(QStringLiteral("播放"));
  audio_.close();
  if (session_ && was_playing) {
    observe(session_->pause(), [this](auto r) {
      if (!r) {
        fail(r.failure_record());
      }
    });
  }
  std::lock_guard lock(mailbox_mutex_);
  pending_.clear();
  ended_ = false;
}

void Window::edited(Result<void> result) {
  if (!result) {
    fail(result.failure_record());
    timeline_->set_project(document_.project());
    return;
  }
  notice_->setText(QStringLiteral("编辑已应用 · Ctrl+Z 撤销"));
  av::ui::set_tone(notice_, av::ui::Tone::Neutral);
  refresh();
}

void Window::refresh() {
  pause();
  ++generation_;
  const auto& p = document_.project();
  {
    QSignalBlocker blocker(magnetic_);
    magnetic_->setChecked(p.magnetic);
  }
  if (editor_duration(p) == 0) {
    preview_->clear_image();
  }
  position_ = std::min(position_, editor_duration(p));
  timeline_->set_project(p);
  timeline_->set_playhead(position_);
  const auto previous =
      assets_->currentItem() ? assets_->currentItem()->data(Qt::UserRole).toString() : QString();
  // Keep generated icons across edits without re-probing media.
  QMap<QString, QIcon> icons;
  for (int i = 0; i < assets_->count(); ++i) {
    auto* item = assets_->item(i);
    icons[item->data(Qt::UserRole).toString()] = item->icon();
  }
  assets_->clear();
  for (const auto& a : p.assets) {
    std::error_code ec;
    const bool missing = !std::filesystem::is_regular_file(std::filesystem::u8path(a.path), ec);
    auto* item = new QListWidgetItem(
        icons.value(text(a.id), av::ui::icon(a.video ? av::ui::Icon::Video : av::ui::Icon::Mic,
                                             QColor("#ADBCD1"))),
        text(a.name) + (missing ? QStringLiteral(" [缺失]") : ""), assets_);
    item->setData(Qt::UserRole, text(a.id));
    item->setToolTip(text(a.path));
    item->setData(Qt::UserRole + 1, a.image ? 3 : a.video ? 1 : 2);
    if (text(a.id) == previous) {
      assets_->setCurrentItem(item);
    }
  }
  if (!assets_->currentItem() && assets_->count()) {
    assets_->setCurrentRow(0);
  }
  filter_assets();
  setWindowTitle(text(p.name) + (document_.dirty() ? " *" : "") + " — FreeCut");
  refresh_properties();
  observe(session_->set_project(p, generation_, position_),
          [this, generation = generation_](auto result) {
            if (generation != generation_) {
              return;
            }
            if (!result) {
              fail(result.failure_record());
            }
          });
  timecode_->setText(QStringLiteral("帧 %1 / %2  ·  %3 s")
                         .arg(position_)
                         .arg(editor_duration(p))
                         .arg(editor_time_us(position_, p.rate) / 1000000., 0, 'f', 3));
}

void Window::new_project() {
  ++document_epoch_;
  path_.clear();
  selected_clip_.clear();
  selected_track_.clear();
  position_ = 0;
  EditorProject p;
  p.tracks = {{id(), "视频 1", false, false, false, false, {}},
              {id(), "音频 1", true, false, false, false, {}}};
  p.main_track_id = p.tracks[0].id;
  p.magnetic = true;
  selected_track_ = p.tracks[0].id;
  auto result = document_.replace(std::move(p));
  if (!result) {
    fail(result.failure_record());
    return;
  }
  refresh();
}

void Window::import_path(const QString& path) {
  pause();
  const auto epoch = document_epoch_;
  notice_->setText(QStringLiteral("正在导入：") + path);
  observe(session_->probe(path.toUtf8().toStdString()), [this, epoch](Result<EditorProbe> result) {
    if (epoch != document_epoch_) {
      return;
    }
    if (!result) {
      fail(result.failure_record());
      emit imported();
      return;
    }
    auto p = document_.project();
    auto existing = std::find_if(p.assets.begin(), p.assets.end(),
                                 [&](const auto& a) { return a.path == result->asset.path; });
    if (existing != p.assets.end()) {
      notice_->setText(QStringLiteral("素材已在库中"));
      emit imported();
      return;
    }
    const auto asset_id = result->asset.id;
    const auto icon = QIcon(QPixmap::fromImage(image(result->thumbnail)));
    timeline_->set_media(asset_id, image(result->thumbnail), result->waveform);
    p.assets.push_back(result->asset);
    edited(document_.edit(std::move(p)));
    for (int i = 0; i < assets_->count(); ++i) {
      if (assets_->item(i)->data(Qt::UserRole).toString() == text(asset_id)) {
        assets_->item(i)->setIcon(icon);
        assets_->setCurrentRow(i);
      }
    }
    emit imported();
  });
}

void Window::add_track(bool audio) {
  auto p = document_.project();
  EditorTrack track;
  track.id = id();
  track.audio_only = audio;
  track.name = (audio ? "音频 " : "视频 ") + std::to_string(p.tracks.size() + 1);
  selected_track_ = track.id;
  selected_clip_.clear();
  p.tracks.push_back(std::move(track));
  edited(document_.edit(std::move(p)));
}

void Window::insert_asset(std::string asset, std::string track, int64_t position) {
  auto p = document_.project();
  const auto a =
      std::find_if(p.assets.begin(), p.assets.end(), [&](const auto& a) { return a.id == asset; });
  if (a == p.assets.end()) {
    return;
  }
  for (auto& t : p.tracks) {
    if (t.id != track) {
      continue;
    }
    EditorClip clip;
    clip.id = id();
    clip.asset_id = asset;
    clip.position = position;
    clip.duration = a->image ? int64_t(p.rate.numerator) * 5 / p.rate.denominator
                             : std::max<int64_t>(1, a->duration_us * p.rate.numerator /
                                                        (1000000LL * p.rate.denominator));
    t.clips.push_back(clip);
    const auto destination = t.id;
    auto result = document_.edit(std::move(p));
    if (result) {
      selected_clip_ = clip.id;
      selected_track_ = destination;
      timeline_->set_selected(clip.id);
    }
    edited(std::move(result));
    return;
  }
}

void Window::insert_selected() {
  if (!assets_->currentItem()) {
    return;
  }
  const auto track = tracks_->currentData().toString().toStdString();
  insert_asset(assets_->currentItem()->data(Qt::UserRole).toString().toStdString(), track,
               position_);
}

void Window::select_clip(std::string clip, std::string track) {
  pause();
  selected_clip_ = std::move(clip);
  selected_track_ = std::move(track);
  refresh_properties();
}

void Window::remove_selected() {
  auto p = document_.project();
  for (auto& t : p.tracks) {
    t.clips.erase(std::remove_if(t.clips.begin(), t.clips.end(),
                                 [&](const auto& c) {
                                   return timeline_->selection().empty()
                                              ? c.id == selected_clip_
                                              : timeline_->selection().count(c.id) != 0;
                                 }),
                  t.clips.end());
  }
  edited(document_.edit(std::move(p)));
}

void Window::refresh_properties() {
  updating_ = true;
  tracks_->clear();
  clips_->clear();
  const EditorTrack* track = nullptr;
  const EditorClip* clip = nullptr;
  for (const auto& t : document_.project().tracks) {
    tracks_->addItem(text(t.name), text(t.id));
    if (t.id == selected_track_) {
      track = &t;
      tracks_->setCurrentIndex(tracks_->count() - 1);
    }
    for (const auto& c : t.clips) {
      auto asset =
          std::find_if(document_.project().assets.begin(), document_.project().assets.end(),
                       [&](const auto& a) { return a.id == c.asset_id; });
      clips_->addItem(
          text(t.name) + " · " +
              (asset == document_.project().assets.end() ? text(c.id) : text(asset->name)) +
              QStringLiteral(" · 帧 %1").arg(c.position),
          text(c.id));
      if (c.id == selected_clip_) {
        clip = &c;
        clips_->setCurrentIndex(clips_->count() - 1);
      }
    }
  }
  locked_->setChecked(track && track->locked);
  hidden_->setChecked(track && track->hidden);
  muted_->setChecked(track && track->muted);
  locked_->setEnabled(track);
  hidden_->setEnabled(track && !track->audio_only);
  muted_->setEnabled(track);
  for (auto* widget : std::vector<QWidget*>{position_input_, source_input_, duration_input_, x_, y_,
                                            scale_, opacity_, volume_}) {
    widget->setEnabled(clip && track && !track->locked);
  }
  if (clip) {
    position_input_->setValue(int(clip->position));
    source_input_->setValue(int(clip->source_in));
    duration_input_->setValue(int(clip->duration));
    x_->setValue(clip->x);
    y_->setValue(clip->y);
    scale_->setValue(clip->scale);
    opacity_->setValue(clip->opacity);
    volume_->setValue(clip->volume);
  }
  preview_->editable = clip && track && !track->locked && !track->audio_only;
  update_effect_properties(clip, clip && track && !track->locked);
  updating_ = false;
}

void Window::apply_properties() {
  auto p = document_.project();
  std::optional<EditorClip> selected;
  for (auto& t : p.tracks) {
    for (auto it = t.clips.begin(); it != t.clips.end(); ++it) {
      if (it->id == selected_clip_) {
        selected = *it;
        t.clips.erase(it);
        break;
      }
    }
  }
  if (!selected) {
    return;
  }
  const int64_t trim_offset =
      int64_t(std::llround((source_input_->value() - selected->source_in) / selected->speed));
  const bool trimmed = trim_offset != 0 || duration_input_->value() != selected->duration;
  selected->position = position_input_->value();
  selected->source_in = source_input_->value();
  selected->duration = duration_input_->value();
  if (trimmed) {
    editor_retime(*selected, trim_offset);
  }
  selected->x = x_->value();
  selected->y = y_->value();
  selected->scale = scale_->value();
  selected->opacity = opacity_->value();
  selected->volume = volume_->value();
  apply_effect_properties(*selected);
  const auto destination = tracks_->currentData().toString().toStdString();
  for (auto& t : p.tracks) {
    if (t.id == destination) {
      t.clips.push_back(*selected);
    }
  }
  auto result = document_.edit(std::move(p));
  if (result) {
    selected_track_ = destination;
  }
  edited(std::move(result));
}

void Window::seek_frame(int64_t frame) {
  pause();
  position_ = std::clamp<int64_t>(frame, 0, editor_duration(document_.project()));
  timeline_->set_playhead(position_);
  observe(session_->seek(position_), [this](auto result) {
    if (!result) {
      fail(result.failure_record());
    }
  });
}

void Window::toggle_play() {
  if (playing_) {
    pause();
    return;
  }
  if (!silent_) {
    audio_.open();
    if (!audio_.warning().isEmpty()) {
      notice_->setText(audio_.warning());
    }
  }
  playing_ = true;
  play_->setText(QStringLiteral("暂停"));
  playback_finished_ = false;
  observe(session_->play(), [this](auto result) {
    if (!result) {
      pause();
      fail(result.failure_record());
    }
  });
}

void Window::poll() {
  Failure error;
  bool ended = false, overflow = false;
  {
    std::lock_guard lock(mailbox_mutex_);
    error = std::move(pending_error_);
    ended = std::exchange(ended_, false);
    overflow = std::exchange(overflow_, false);
    while (!pending_.empty()) {
      auto frame = pending_.front();
      if (frame->generation != generation_) {
        pending_.pop_front();
        continue;
      }
      if (!playing_ && frame->frame != position_) {
        pending_.pop_front();
        continue;
      }
      if (playing_ && !silent_ && !audio_.canAccept()) {
        break;
      }
      if (editor_duration(document_.project()) > 0) {
        preview_->set_image(frame->image);
      }
      position_ = frame->frame;
      timeline_->set_playhead(position_);
      timecode_->setText(
          QStringLiteral("帧 %1 / %2  ·  %3 s")
              .arg(position_)
              .arg(editor_duration(document_.project()))
              .arg(editor_time_us(position_, document_.project().rate) / 1000000., 0, 'f', 3));
      if (playing_ && !silent_) {
        media::AudioFrame output;
        output.pcm = std::make_shared<const std::vector<float>>(frame->audio);
        audio_.enqueue(output);
      }
      pending_.pop_front();
    }
  }
  audio_.pump();
  if (overflow) {
    pause();
    notice_->setText(QStringLiteral("预览输出缓冲已满，已暂停；可降低工程分辨率后继续。"));
  }
  if (error) {
    pause();
    fail(error);
  }
  if (ended) {
    playback_finished_ = true;
  }
  if (playback_finished_) {
    bool empty;
    {
      std::lock_guard lock(mailbox_mutex_);
      empty = pending_.empty();
    }
    if (empty && (silent_ || audio_.drained())) {
      playing_ = false;
      playback_finished_ = false;
      play_->setText(QStringLiteral("播放"));
    }
  }
}

bool Window::save_path(const QString& path) {
  auto result = editor_save(file_path(path), document_.project());
  if (!result) {
    fail(result.failure_record());
    return false;
  }
  path_ = path;
  document_.mark_saved();
  setWindowTitle(text(document_.project().name) + " — FreeCut");
  notice_->setText(QStringLiteral("工程已保存：") + path);
  return true;
}

bool Window::save() {
  if (!path_.isEmpty()) {
    return save_path(path_);
  }
  auto path = QFileDialog::getSaveFileName(this, QStringLiteral("保存工程"), {},
                                           "FreeCut (*.freecut.json)");
  if (path.isEmpty()) {
    return false;
  }
  return save_path(path.endsWith(".freecut.json") ? path : path + ".freecut.json");
}

bool Window::load_path(const QString& path) {
  auto result = editor_load(file_path(path));
  if (!result) {
    fail(result.failure_record());
    return false;
  }
  auto replaced = document_.replace(std::move(*result));
  if (!replaced) {
    fail(replaced.failure_record());
    return false;
  }
  ++document_epoch_;
  path_ = path;
  selected_clip_.clear();
  position_ = 0;
  selected_track_ = document_.project().tracks.empty() ? "" : document_.project().tracks[0].id;
  refresh();
  notice_->setText(QStringLiteral("工程已打开；缺失素材可在素材库中重新定位。"));
  load_thumbnails(0, document_epoch_);
  return true;
}

void Window::load_thumbnails(size_t index, uint64_t epoch) {
  if (epoch != document_epoch_) {
    return;
  }
  const auto& assets = document_.project().assets;
  while (index < assets.size() && !assets[index].video) {
    ++index;
  }
  if (index == assets.size()) {
    return;
  }
  const auto asset = assets[index];
  observe(session_->probe(asset.path), [this, index, epoch, asset](Result<EditorProbe> result) {
    if (epoch != document_epoch_) {
      return;
    }
    if (result) {
      for (int row = 0; row < assets_->count(); ++row) {
        auto* item = assets_->item(row);
        if (item->data(Qt::UserRole).toString() == text(asset.id)) {
          item->setIcon(QIcon(QPixmap::fromImage(image(result->thumbnail))));
          timeline_->set_media(item->data(Qt::UserRole).toString().toStdString(),
                               image(result->thumbnail), result->waveform);
        }
      }
    }
    load_thumbnails(index + 1, epoch);
  });
}

void Window::relink() {
  if (!assets_->currentItem()) {
    return;
  }
  const auto asset_id = assets_->currentItem()->data(Qt::UserRole).toString().toStdString();
  const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("重新定位素材"));
  if (path.isEmpty()) {
    return;
  }
  const auto epoch = document_epoch_;
  observe(session_->probe(path.toUtf8().toStdString()),
          [this, asset_id, epoch](Result<EditorProbe> result) {
            if (epoch != document_epoch_) {
              return;
            }
            if (!result) {
              fail(result.failure_record());
              return;
            }
            auto p = document_.project();
            for (auto& a : p.assets) {
              if (a.id == asset_id) {
                auto replacement = result->asset;
                replacement.id = a.id;
                a = std::move(replacement);
              }
            }
            edited(document_.edit(std::move(p)));
          });
}

void Window::export_path(const QString& path) {
  if (exporting_) {
    return;
  }
  pause();
  exporting_ = true;
  export_button_->setEnabled(false);
  progress_->setValue(0);
  progress_->show();
  cancel_export_->show();
  observe(exporter_->start(document_.project(), file_path(path),
                           [this](auto progress) {
                             QMetaObject::invokeMethod(
                                 this,
                                 [this, progress] {
                                   progress_->setValue(int(progress.completed_frames * 1000 /
                                                           progress.total_frames));
                                 },
                                 Qt::QueuedConnection);
                           }),
          [this, path](Result<void> result) {
            exporting_ = false;
            export_button_->setEnabled(true);
            cancel_export_->hide();
            if (result) {
              progress_->setValue(1000);
              notice_->setText(QStringLiteral("导出完成：") + path);
            } else if (result.error().code == ErrorCode::cancelled) {
              notice_->setText(QStringLiteral("导出已取消"));
              progress_->hide();
            } else {
              fail(result.failure_record());
            }
            emit export_finished(bool(result));
          });
}

bool Window::confirm_save() {
  if (!document_.dirty()) {
    return true;
  }
  av::ui::Dialog dialog(this, QStringLiteral("保存工程更改？"),
                        QStringLiteral("尚未保存的编辑将在关闭工程后丢失。"));
  auto* save_button = dialog.add_action(QStringLiteral("保存"), av::ui::ButtonRole::Primary);
  auto* discard = dialog.add_action(QStringLiteral("放弃更改"));
  auto* cancel = dialog.add_action(QStringLiteral("取消"));
  cancel->setDefault(true);
  connect(save_button, &QPushButton::clicked, &dialog, [&] {
    if (save()) {
      dialog.done(1);
    }
  });
  connect(discard, &QPushButton::clicked, &dialog, [&] { dialog.done(2); });
  connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  return dialog.exec() != 0;
}

void Window::closeEvent(QCloseEvent* e) {
  if (!confirm_save()) {
    e->ignore();
    return;
  }
  exporter_->cancel();
  pause();
  e->accept();
}

void Window::dragEnterEvent(QDragEnterEvent* e) {
  if (e->mimeData()->hasUrls()) {
    e->acceptProposedAction();
  }
}

void Window::dropEvent(QDropEvent* e) {
  for (const auto& url : e->mimeData()->urls()) {
    if (url.isLocalFile()) {
      import_path(url.toLocalFile());
    }
  }
  e->acceptProposedAction();
}

void Window::project_settings() {
  av::ui::Dialog dialog(this, QStringLiteral("工程设置"),
                        QStringLiteral("分辨率上限 4096，必须为偶数。帧率仅可在空时间线中修改。"));
  auto* form = new QFormLayout;
  dialog.content_layout()->addLayout(form);
  auto spin = [&](QString name, int min, int max, int value) {
    auto* s = new QSpinBox(&dialog);
    s->setRange(min, max);
    s->setValue(value);
    form->addRow(name, s);
    return s;
  };
  const auto& p = document_.project();
  auto* width = spin(QStringLiteral("宽"), 2, 4096, p.width);
  width->setSingleStep(2);
  auto* height = spin(QStringLiteral("高"), 2, 4096, p.height);
  height->setSingleStep(2);
  auto* num = spin(QStringLiteral("帧率分子"), 1, 240000, p.rate.numerator);
  auto* den = spin(QStringLiteral("帧率分母"), 1, 1001000, p.rate.denominator);
  num->setEnabled(editor_duration(p) == 0);
  den->setEnabled(editor_duration(p) == 0);
  auto* apply = dialog.add_action(QStringLiteral("应用"), av::ui::ButtonRole::Primary);
  auto* cancel = dialog.add_action(QStringLiteral("取消"));
  connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  connect(apply, &QPushButton::clicked, &dialog, [&] {
    auto changed = document_.project();
    changed.width = width->value();
    changed.height = height->value();
    changed.rate = {num->value(), den->value()};
    auto result = document_.edit(std::move(changed));
    if (!result) {
      dialog.set_error(text(result.error().message));
      return;
    }
    refresh();
    dialog.accept();
  });
  dialog.exec();
}
}  // namespace freecut
