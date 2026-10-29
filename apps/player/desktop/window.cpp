#include "window.hpp"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QScreen>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <algorithm>

static void initPlayerResources() {
  Q_INIT_RESOURCE(resources);
}

namespace player_demo {
namespace {
QString clockText(qint64 us) {
  const auto seconds = std::max<qint64>(0, us / 1000000);
  return QStringLiteral("%1:%2:%3")
      .arg(seconds / 3600, 2, 10, QChar('0'))
      .arg(seconds / 60 % 60, 2, 10, QChar('0'))
      .arg(seconds % 60, 2, 10, QChar('0'));
}

QIcon icon(const QString& name) {
  return QIcon(":/player/icons/" + name + ".svg");
}

QToolButton* button(QAction* action, QWidget* parent) {
  auto* button = new QToolButton(parent);
  button->setDefaultAction(action);
  button->setIconSize({20, 20});
  button->setMinimumSize(34, 32);
  button->setAccessibleName(action->text());
  return button;
}
}  // namespace

PlayerWindow::PlayerWindow(const QString& settingsFile, bool automation,
                           std::unique_ptr<AudioOutput> audio, QWidget* parent)
    : QMainWindow(parent),
      controller_(std::move(audio)),
      playlist_(),
      settings_(settingsFile),
      automation_(automation) {
  initPlayerResources();
  setObjectName("playerWindow");
  setWindowTitle(QStringLiteral("本地播放器"));
  resize(1080, 720);
  setMinimumSize(720, 480);
  buildUi();
  if (!automation_) {
    playlist_.replace(settings_.value("playlist").toStringList());
    restoreGeometry(settings_.value("geometry").toByteArray());
    if (isFullScreen())
      setWindowState(Qt::WindowNoState);
    if (!screen()->availableGeometry().intersects(frameGeometry()))
      move(screen()->availableGeometry().topLeft() + QPoint(40, 40));
    splitter_->restoreState(settings_.value("splitter").toByteArray());
  }
  connect(&controller_, &Controller::frameChanged, video_, &VideoWidget::setFrame);
  connect(&controller_, &Controller::changed, this, &PlayerWindow::refresh);
  connect(&controller_, &Controller::opened, this, [this] {
    if (!pendingSubtitle_.isEmpty()) {
      if (QFileInfo(controller_.path()).absoluteFilePath() == subtitleMedia_)
        controller_.loadSubtitles(pendingSubtitle_);
      pendingSubtitle_.clear();
      subtitleMedia_.clear();
    }
    playlist_.markError(playlist_.currentId(), {});
    video_->showOsd(QFileInfo(controller_.path()).fileName());
  });
  connect(&controller_, &Controller::ended, this, [this] {
    remember();
    if (autoAdvance_)
      advance(false);
  });
  connect(&controller_, &Controller::failed, this, [this](const QString& error) {
    playlist_.markError(playlist_.currentId(), error);
    const auto failedId = playlist_.currentId();
    if (autoAdvance_)
      QTimer::singleShot(0, this, [this, failedId] {
        if (playlist_.currentId() == failedId && !controller_.error().isEmpty())
          advance(true);
      });
  });
  connect(&controller_, &Controller::closed, this, [this] {
    close();
    emit shutdownComplete();
  });
  connect(&playlist_, &PlaylistModel::filesDropped, this,
          [this](const QStringList& paths) { openFiles(paths, true); });
  connect(video_, &VideoWidget::filesDropped, this,
          [this](const QStringList& paths) { openFiles(paths); });
  connect(video_, &VideoWidget::doubleClicked, this, &PlayerWindow::toggleFullscreen);
  connect(list_, &QListView::doubleClicked, this,
          [this](const QModelIndex& index) { playRow(index.row()); });
  qApp->installEventFilter(this);
  idle_.start();
  idleTimer_.setInterval(250);
  connect(&idleTimer_, &QTimer::timeout, this, &PlayerWindow::checkIdle);
  idleTimer_.start();
  historyTimer_.setInterval(5000);
  connect(&historyTimer_, &QTimer::timeout, this, [this] {
    remember();
    settings_.sync();
  });
  if (!automation_)
    historyTimer_.start();
  updateVolume();
  refresh();
}

QAction* PlayerWindow::action(const char* name, const QString& text, const QString& glyph,
                              const QKeySequence& shortcut) {
  auto* value = new QAction(glyph.isEmpty() ? QIcon{} : icon(glyph), text, this);
  value->setObjectName(name);
  value->setShortcut(shortcut);
  value->setToolTip(
      text + (shortcut.isEmpty() ? "" : " (" + shortcut.toString(QKeySequence::NativeText) + ")"));
  addAction(value);
  return value;
}

void PlayerWindow::buildUi() {
  setStyleSheet(R"(
    QMainWindow, QWidget#root { background: #171b22; color: #e1e8f2; }
    QWidget { font-size: 13px; }
    QMenuBar, QMenu, QDialog { background: #202630; color: #e1e8f2; }
    QMenuBar::item:selected, QMenu::item:selected { background: #315e95; }
    QMenu::item { padding: 7px 28px 7px 18px; }
    QMenu::item:disabled { color: #738092; }
    QWidget#controls, QWidget#listPanel { background: #202630; }
    QLabel { color: #bdc8d9; background: transparent; }
    QLabel#banner { background: #352c25; color: #ffe0ad; padding: 8px 12px; }
    QToolButton, QPushButton { color: #e1e8f2; background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 4px; }
    QToolButton:hover, QPushButton:hover { background: #354254; }
    QToolButton:pressed, QToolButton:checked { background: #315e95; }
    QToolButton:focus, QPushButton:focus, QListView:focus, QSlider:focus { border: 1px solid #83b8ff; }
    QToolButton:disabled, QPushButton:disabled { color: #738092; }
    QSlider { border: 1px solid transparent; }
    QSlider::groove:horizontal { background: #465367; height: 4px; border-radius: 2px; }
    QSlider::sub-page:horizontal { background: #6aa9ff; border-radius: 2px; }
    QSlider::handle:horizontal { background: #c9dfff; width: 12px; margin: -5px 0; border-radius: 6px; }
    QListView { background: #202630; color: #cbd5e3; border: 1px solid transparent; outline: 0; }
    QListView::item { padding: 9px 6px; }
    QListView::item:selected { background: #315e95; color: #fff; }
    QListView::item:hover { background: #2c394a; }
    QSplitter::handle { background: #364151; width: 1px; }
    QSpinBox { color: #e1e8f2; background: #171b22; padding: 5px; }
    QCheckBox { color: #e1e8f2; padding: 6px; }
    QToolTip { background: #293446; color: #fff; border: 1px solid #526986; }
  )");
  auto* open = action("open", QStringLiteral("打开文件…"), "open", QKeySequence::Open);
  auto* add = action("add", QStringLiteral("添加文件…"), "add");
  play_ = action("play", QStringLiteral("播放 / 暂停"), "play", Qt::Key_Space);
  stop_ = action("stop", QStringLiteral("停止"), "stop", Qt::Key_S);
  previous_ = action("previous", QStringLiteral("上一集"), "previous",
                     QKeySequence(Qt::CTRL | Qt::Key_Left));
  next_ = action("next", QStringLiteral("下一集"), "next", QKeySequence(Qt::CTRL | Qt::Key_Right));
  auto* back = action("back", QStringLiteral("后退 5 秒"), {}, Qt::Key_Left);
  auto* forward = action("forward", QStringLiteral("前进 5 秒"), {}, Qt::Key_Right);
  mute_ = action("mute", QStringLiteral("静音"), "volume", Qt::Key_M);
  mute_->setCheckable(true);
  fullscreen_ = action("fullscreen", QStringLiteral("全屏"), "fullscreen", Qt::Key_F);
  fullscreen_->setCheckable(true);
  listToggle_ = action("playlist", QStringLiteral("播放列表"), "list", Qt::Key_F6);
  listToggle_->setCheckable(true);
  listToggle_->setChecked(settings_.value("listVisible", true).toBool());
  auto* subtitle = action("loadSubtitle", QStringLiteral("加载字幕…"), "subtitle");
  subtitleToggle_ = action("subtitleVisible", QStringLiteral("显示字幕"), {}, Qt::Key_V);
  subtitleToggle_->setCheckable(true);
  subtitleToggle_->setChecked(true);
  auto* subSettings = action("subtitleSettings", QStringLiteral("字幕设置…"));
  auto* preferences =
      action("settings", QStringLiteral("设置…"), "settings", QKeySequence::Preferences);
  auto* remove = action("remove", QStringLiteral("删除所选"), "remove");
  auto* clear = action("clear", QStringLiteral("清空列表"));
  auto* up = action("moveUp", QStringLiteral("向上移动"), "up");
  auto* down = action("moveDown", QStringLiteral("向下移动"), "down");
  auto* quit = action("quit", QStringLiteral("退出"), {}, QKeySequence::Quit);
  auto* escape = action("escape", QStringLiteral("退出全屏"), {}, Qt::Key_Escape);
  connect(open, &QAction::triggered, this, [this] { browse(false); });
  connect(add, &QAction::triggered, this, [this] { browse(true); });
  connect(play_, &QAction::triggered, this, [this] {
    if ((controller_.snapshot().state == sdk::PlayerState::closed ||
         !controller_.error().isEmpty()) &&
        playlist_.rowCount())
      playRow(std::max(0, playlist_.rowForId(playlist_.currentId())));
    else
      controller_.togglePlay();
  });
  connect(stop_, &QAction::triggered, &controller_, &Controller::stop);
  connect(previous_, &QAction::triggered, this, [this] { next(true); });
  connect(next_, &QAction::triggered, this, [this] { next(); });
  connect(back, &QAction::triggered, this, [this] {
    controller_.seek(controller_.snapshot().position_us - 5000000);
    video_->showOsd(QStringLiteral("后退 5 秒"));
  });
  connect(forward, &QAction::triggered, this, [this] {
    controller_.seek(controller_.snapshot().position_us + 5000000);
    video_->showOsd(QStringLiteral("前进 5 秒"));
  });
  connect(mute_, &QAction::toggled, this, &PlayerWindow::updateVolume);
  connect(fullscreen_, &QAction::triggered, this, &PlayerWindow::toggleFullscreen);
  connect(escape, &QAction::triggered, this, [this] {
    if (isFullScreen())
      toggleFullscreen();
  });
  connect(listToggle_, &QAction::toggled, this, &PlayerWindow::updateListVisibility);
  connect(subtitle, &QAction::triggered, this, &PlayerWindow::browseSubtitles);
  connect(subtitleToggle_, &QAction::toggled, this, [this](bool enabled) {
    controller_.subtitles.enabled = enabled;
    video_->showOsd(enabled ? QStringLiteral("字幕已显示") : QStringLiteral("字幕已隐藏"));
  });
  connect(subSettings, &QAction::triggered, this, &PlayerWindow::subtitleSettings);
  connect(preferences, &QAction::triggered, this, &PlayerWindow::showSettings);
  connect(remove, &QAction::triggered, this, &PlayerWindow::removeSelected);
  connect(clear, &QAction::triggered, this, [this] {
    remember();
    controller_.stop();
    playlist_.replace({});
  });
  connect(up, &QAction::triggered, this, [this] {
    const auto row = list_->currentIndex().row();
    playlist_.moveRows({}, row, 1, {}, row - 1);
  });
  connect(down, &QAction::triggered, this, [this] {
    const auto row = list_->currentIndex().row();
    playlist_.moveRows({}, row, 1, {}, row + 2);
  });
  connect(quit, &QAction::triggered, this, &QWidget::close);

  auto* fileMenu = menuBar()->addMenu(QStringLiteral("文件"));
  fileMenu->addActions({open, add, preferences});
  fileMenu->addSeparator();
  fileMenu->addAction(quit);
  auto* playbackMenu = menuBar()->addMenu(QStringLiteral("播放"));
  playbackMenu->addActions({play_, stop_, previous_, next_, back, forward});
  auto* repeatMenu = playbackMenu->addMenu(QStringLiteral("播放模式"));
  auto* group = new QActionGroup(this);
  repeat_ = static_cast<RepeatMode>(std::clamp(settings_.value("repeat", 0).toInt(), 0, 2));
  const QStringList modes{QStringLiteral("顺序播放"), QStringLiteral("单曲循环"),
                          QStringLiteral("列表循环")};
  for (int i = 0; i < modes.size(); ++i) {
    auto* value = action(qPrintable("repeat" + QString::number(i)), modes[i]);
    value->setCheckable(true);
    group->addAction(value);
    repeatMenu->addAction(value);
    value->setChecked(i == static_cast<int>(repeat_));
    connect(value, &QAction::triggered, this, [this, i] { repeat_ = static_cast<RepeatMode>(i); });
  }
  auto* audioMenu = menuBar()->addMenu(QStringLiteral("声音"));
  audioMenu->addAction(mute_);
  auto* subMenu = menuBar()->addMenu(QStringLiteral("字幕"));
  subMenu->addActions({subtitle, subtitleToggle_, subSettings});
  auto* viewMenu = menuBar()->addMenu(QStringLiteral("视图"));
  viewMenu->addActions({fullscreen_, listToggle_});
  contextMenu_ = new QMenu(this);
  contextMenu_->addActions({open, play_, stop_, previous_, next_});
  contextMenu_->addSeparator();
  contextMenu_->addMenu(repeatMenu);
  contextMenu_->addMenu(subMenu);
  contextMenu_->addActions({mute_, fullscreen_, listToggle_, preferences});

  auto* root = new QWidget(this);
  root->setObjectName("root");
  setCentralWidget(root);
  auto* layout = new QVBoxLayout(root);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  banner_ = new QLabel(root);
  banner_->setObjectName("banner");
  banner_->setWordWrap(true);
  banner_->setTextFormat(Qt::PlainText);
  banner_->hide();
  layout->addWidget(banner_);
  splitter_ = new QSplitter(root);
  layout->addWidget(splitter_, 1);
  video_ = new VideoWidget(splitter_);
  video_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(video_, &QWidget::customContextMenuRequested, this, [this](const QPoint& p) {
    revealControls();
    contextMenu_->popup(video_->mapToGlobal(p));
  });
  listPanel_ = new QWidget(splitter_);
  listPanel_->setObjectName("listPanel");
  listPanel_->setMinimumWidth(180);
  auto* listLayout = new QVBoxLayout(listPanel_);
  listLayout->setContentsMargins(8, 10, 8, 8);
  listLayout->setSpacing(6);
  count_ = new QLabel(QStringLiteral("播放列表"), listPanel_);
  listLayout->addWidget(count_);
  list_ = new QListView(listPanel_);
  list_->setObjectName("playlistView");
  list_->setAccessibleName(QStringLiteral("播放列表"));
  list_->setModel(&playlist_);
  list_->setDragEnabled(true);
  list_->setAcceptDrops(true);
  list_->setDropIndicatorShown(true);
  list_->setDragDropMode(QAbstractItemView::DragDrop);
  list_->setDefaultDropAction(Qt::MoveAction);
  list_->setContextMenuPolicy(Qt::ActionsContextMenu);
  list_->addActions({add, remove, clear, up, down});
  listLayout->addWidget(list_, 1);
  auto* listButtons = new QHBoxLayout;
  listButtons->setSpacing(4);
  for (auto* value : {add, remove, up, down})
    listButtons->addWidget(button(value, listPanel_));
  listButtons->addStretch();
  listLayout->addLayout(listButtons);
  splitter_->setStretchFactor(0, 1);
  splitter_->setStretchFactor(1, 0);
  splitter_->setSizes({800, 280});
  splitter_->setChildrenCollapsible(false);
  controls_ = new QWidget(root);
  controls_->setObjectName("controls");
  layout->addWidget(controls_);
  auto* controlLayout = new QVBoxLayout(controls_);
  controlLayout->setContentsMargins(12, 7, 12, 8);
  controlLayout->setSpacing(4);
  auto* timeline = new QHBoxLayout;
  progress_ = new QSlider(Qt::Horizontal, controls_);
  progress_->setObjectName("progress");
  progress_->setAccessibleName(QStringLiteral("播放进度"));
  progress_->setRange(0, 10000);
  progress_->setMinimumHeight(24);
  time_ = new QLabel("00:00:00 / 00:00:00", controls_);
  time_->setMinimumWidth(140);
  timeline->addWidget(progress_, 1);
  timeline->addWidget(time_);
  controlLayout->addLayout(timeline);
  connect(progress_, &QSlider::sliderReleased, this, [this] {
    controller_.seek(controller_.snapshot().media.duration_us * progress_->value() / 10000);
  });
  connect(progress_, &QSlider::actionTriggered, this, [this](int action) {
    if (action != QAbstractSlider::SliderMove && !progress_->isSliderDown())
      controller_.seek(controller_.snapshot().media.duration_us * progress_->sliderPosition() /
                       10000);
  });
  auto* transport = new QHBoxLayout;
  transport->setSpacing(5);
  for (auto* value : {open, previous_, play_, stop_, next_})
    transport->addWidget(button(value, controls_));
  transport->addStretch();
  transport->addWidget(button(mute_, controls_));
  volume_ = new QSlider(Qt::Horizontal, controls_);
  volume_->setObjectName("volume");
  volume_->setAccessibleName(QStringLiteral("音量"));
  volume_->setFixedWidth(100);
  volume_->setRange(0, 100);
  volume_->setValue(std::clamp(settings_.value("volume", 80).toInt(), 0, 100));
  connect(volume_, &QSlider::valueChanged, this, &PlayerWindow::updateVolume);
  transport->addWidget(volume_);
  for (auto* value : {subtitle, listToggle_, fullscreen_})
    transport->addWidget(button(value, controls_));
  controlLayout->addLayout(transport);
  subtitleSize_ = std::clamp(settings_.value("subtitleSize", 28).toInt(), 16, 64);
  mute_->setChecked(settings_.value("muted", false).toBool());
  updateListVisibility();
}

void PlayerWindow::browse(bool append) {
  const auto paths = QFileDialog::getOpenFileNames(
      this, append ? QStringLiteral("添加到播放列表") : QStringLiteral("打开媒体文件"), {},
      QStringLiteral("媒体文件 (*.mp4 *.mkv *.avi *.mov *.webm *.wmv *.flv *.ts *.m4v *.mp3 *.wav "
                     "*.flac *.aac *.m4a *.ogg *.opus);;所有文件 (*)"));
  if (!paths.isEmpty())
    openFiles(paths, append);
}

void PlayerWindow::openFiles(const QStringList& paths, bool append) {
  if (exiting_ || paths.isEmpty())
    return;
  QStringList media, subtitles;
  for (const auto& path : paths) {
    if (Subtitles::accepts(path.toUtf8().toStdString()))
      subtitles << path;
    else if (QFileInfo(path).isDir())
      video_->showOsd(QStringLiteral("请拖入文件，而非文件夹"));
    else
      media << path;
  }
  if (!media.isEmpty()) {
    if (append)
      playlist_.append(media);
    else {
      remember();
      playlist_.replace(media);
      playRow(0);
    }
  }
  if (!subtitles.isEmpty()) {
    if (!media.isEmpty() && !append) {
      pendingSubtitle_ = subtitles.first();
      subtitleMedia_ = QFileInfo(media.first()).absoluteFilePath();
    } else {
      controller_.loadSubtitles(subtitles.first());
      refresh();
    }
  }
  refresh();
}

void PlayerWindow::playRow(int row, bool automatic) {
  const auto entry = playlist_.item(row);
  if (entry.path.isEmpty() || exiting_)
    return;
  remember();
  pendingSubtitle_.clear();
  subtitleMedia_.clear();
  if (!automatic)
    failedIds_.clear();
  playlist_.setCurrent(entry.id);
  list_->setCurrentIndex(playlist_.index(row));
  controller_.open(entry.path, automation_ || automatic ? 0 : settings_.resumePosition(entry.path));
}

void PlayerWindow::next(bool previous) {
  int row = playlist_.rowForId(playlist_.currentId());
  row = row < 0 ? 0 : row + (previous ? -1 : 1);
  if (repeat_ == RepeatMode::All && playlist_.rowCount())
    row = (row + playlist_.rowCount()) % playlist_.rowCount();
  playRow(row);
}

void PlayerWindow::advance(bool failed) {
  if (exiting_ || playlist_.currentId().isEmpty())
    return;
  if (failed)
    failedIds_.insert(playlist_.currentId());
  else
    failedIds_.clear();
  const int row = playlist_.nextRow(repeat_, failed);
  if (row < 0 || failedIds_.contains(playlist_.item(row).id))
    return;
  playRow(row, true);
}

void PlayerWindow::removeSelected() {
  const int row = list_->currentIndex().row();
  if (row < 0)
    return;
  if (playlist_.item(row).id == playlist_.currentId()) {
    remember();
    controller_.stop();
  }
  playlist_.removeRows(row, 1);
  refresh();
}

void PlayerWindow::browseSubtitles() {
  const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("加载外挂字幕"), {},
                                                 QStringLiteral("字幕文件 (*.srt *.vtt)"));
  if (!path.isEmpty()) {
    controller_.loadSubtitles(path);
    refresh();
  }
}

void PlayerWindow::subtitleSettings() {
  QDialog dialog(this);
  dialog.setWindowTitle(QStringLiteral("字幕设置"));
  auto* form = new QFormLayout(&dialog);
  auto* enabled = new QCheckBox(QStringLiteral("显示字幕"), &dialog);
  enabled->setChecked(controller_.subtitles.enabled);
  form->addRow(enabled);
  auto* delay = new QSpinBox(&dialog);
  delay->setRange(-600000, 600000);
  delay->setSingleStep(100);
  delay->setSuffix(" ms");
  delay->setValue(controller_.subtitles.delay_ms);
  form->addRow(QStringLiteral("时间偏移（正值延后）"), delay);
  auto* size = new QSpinBox(&dialog);
  size->setRange(16, 64);
  size->setValue(subtitleSize_);
  form->addRow(QStringLiteral("字幕字号"), size);
  auto* reset = new QPushButton(QStringLiteral("重置偏移"), &dialog);
  form->addRow(reset);
  connect(reset, &QPushButton::clicked, delay, [delay] { delay->setValue(0); });
  auto* remove = new QPushButton(QStringLiteral("移除当前字幕"), &dialog);
  form->addRow(remove);
  connect(remove, &QPushButton::clicked, &dialog, [this, &dialog] {
    controller_.subtitles.clear();
    controller_.subtitleError.clear();
    dialog.reject();
  });
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() == QDialog::Accepted) {
    controller_.subtitles.enabled = enabled->isChecked();
    controller_.subtitles.delay_ms = delay->value();
    subtitleSize_ = size->value();
  }
  refresh();
}

void PlayerWindow::showSettings() {
  QDialog dialog(this);
  dialog.setWindowTitle(QStringLiteral("播放器设置"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* resume = new QCheckBox(QStringLiteral("重新打开文件时继续上次播放"), &dialog);
  resume->setChecked(settings_.value("resumeEnabled", true).toBool());
  layout->addWidget(resume);
  auto* clear = new QPushButton(QStringLiteral("清除播放历史"), &dialog);
  layout->addWidget(clear);
  connect(clear, &QPushButton::clicked, this, [this] {
    settings_.clearHistory();
    video_->showOsd(QStringLiteral("播放历史已清除"));
  });
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() == QDialog::Accepted)
    settings_.set("resumeEnabled", resume->isChecked());
}

void PlayerWindow::updateVolume() {
  if (!volume_)
    return;
  controller_.setVolume(volume_->value(), mute_->isChecked());
  volume_->setToolTip(QString::number(volume_->value()) + "%");
}

void PlayerWindow::refresh() {
  const auto state = controller_.snapshot();
  const bool loaded = state.state != sdk::PlayerState::closed &&
                      state.state != sdk::PlayerState::failed && controller_.error().isEmpty();
  const bool playing = state.state == sdk::PlayerState::playing;
  static const QIcon playIcon = icon("play"), pauseIcon = icon("pause");
  const auto& desiredIcon = playing ? pauseIcon : playIcon;
  if (play_->icon().cacheKey() != desiredIcon.cacheKey())
    play_->setIcon(desiredIcon);
  play_->setEnabled(!controller_.busy() && !exiting_ && (loaded || playlist_.rowCount()));
  stop_->setEnabled(loaded && !exiting_);
  const int currentRow = playlist_.rowForId(playlist_.currentId());
  const bool canWrap = repeat_ == RepeatMode::All && playlist_.rowCount() > 0;
  previous_->setEnabled(!exiting_ && (currentRow > 0 || canWrap));
  next_->setEnabled(!exiting_ && (currentRow + 1 < playlist_.rowCount() || canWrap));
  for (const char* name : {"back", "forward"})
    findChild<QAction*>(name)->setEnabled(loaded && !exiting_);
  findChild<QAction*>("loadSubtitle")->setEnabled(loaded && !controller_.busy() && !exiting_);
  subtitleToggle_->setEnabled(controller_.subtitles.size() > 0);
  const int selectedRow = list_->currentIndex().row();
  findChild<QAction*>("remove")->setEnabled(selectedRow >= 0);
  findChild<QAction*>("clear")->setEnabled(playlist_.rowCount() > 0);
  findChild<QAction*>("moveUp")->setEnabled(selectedRow > 0);
  findChild<QAction*>("moveDown")
      ->setEnabled(selectedRow >= 0 && selectedRow + 1 < playlist_.rowCount());
  progress_->setEnabled(loaded && state.media.duration_us > 0 && !exiting_);
  if (!progress_->isSliderDown()) {
    QSignalBlocker block(progress_);
    progress_->setValue(
        state.media.duration_us > 0 ? state.position_us * 10000 / state.media.duration_us : 0);
  }
  time_->setText(clockText(state.position_us) + " / " + clockText(state.media.duration_us));
  const QString subtitle = QString::fromStdString(controller_.subtitles.text_at(state.position_us));
  video_->setSubtitle(subtitle, subtitleSize_);
  {
    QSignalBlocker block(subtitleToggle_);
    subtitleToggle_->setChecked(controller_.subtitles.enabled);
  }
  video_->setMessage(controller_.busy()             ? QStringLiteral("正在处理…")
                     : controller_.path().isEmpty() ? QString{}
                                                    : QFileInfo(controller_.path()).fileName());
  QString message = controller_.error();
  if (!controller_.subtitleError.isEmpty())
    message = controller_.subtitleError;
  if (message.isEmpty())
    message = controller_.warning();
  banner_->setText(message);
  banner_->setVisible(!message.isEmpty());
  count_->setText(QStringLiteral("播放列表 · %1 项").arg(playlist_.rowCount()));
  setWindowTitle(controller_.path().isEmpty()
                     ? QStringLiteral("本地播放器")
                     : QFileInfo(controller_.path()).fileName() + QStringLiteral(" — 本地播放器"));
}

void PlayerWindow::remember() {
  if (automation_ || exiting_ || controller_.busy() || !controller_.error().isEmpty())
    return;
  const auto state = controller_.snapshot();
  if (state.state == sdk::PlayerState::closed || state.state == sdk::PlayerState::failed)
    return;
  settings_.remember(controller_.path(), state.position_us, state.media.duration_us,
                     state.state == sdk::PlayerState::ended);
}

void PlayerWindow::saveState() {
  if (automation_ || saved_)
    return;
  remember();
  settings_.set("geometry",
                isFullScreen() && !normalGeometry_.isEmpty() ? normalGeometry_ : saveGeometry());
  settings_.set("splitter", splitter_->saveState());
  settings_.set("playlist", playlist_.paths());
  settings_.set("volume", volume_->value());
  settings_.set("muted", mute_->isChecked());
  settings_.set("listVisible", listToggle_->isChecked());
  settings_.set("subtitleSize", subtitleSize_);
  settings_.set("repeat", static_cast<int>(repeat_));
  settings_.sync();
  saved_ = true;
}

void PlayerWindow::closeEvent(QCloseEvent* event) {
  if (controller_.finished()) {
    event->accept();
    return;
  }
  event->ignore();
  saveState();
  exiting_ = true;
  historyTimer_.stop();
  controller_.close();
}

void PlayerWindow::toggleFullscreen() {
  if (isFullScreen()) {
    showNormal();
    menuBar()->show();
  } else {
    normalGeometry_ = saveGeometry();
    showFullScreen();
    menuBar()->hide();
  }
  fullscreen_->setChecked(isFullScreen());
  updateListVisibility();
  revealControls();
}

void PlayerWindow::updateListVisibility() {
  if (listPanel_)
    listPanel_->setVisible(listToggle_->isChecked() && !isFullScreen());
}

void PlayerWindow::revealControls() {
  idle_.restart();
  controls_->show();
  video_->unsetCursor();
}

void PlayerWindow::checkIdle() {
  if (!isFullScreen() || idle_.elapsed() < 3000 || QApplication::activePopupWidget() ||
      QApplication::activeModalWidget() || progress_->isSliderDown() || volume_->isSliderDown())
    return;
  if (auto* focus = QApplication::focusWidget(); focus && controls_->isAncestorOf(focus))
    return;
  controls_->hide();
  video_->setCursor(Qt::BlankCursor);
}

bool PlayerWindow::eventFilter(QObject* watched, QEvent* event) {
  auto* widget = qobject_cast<QWidget*>(watched);
  if (widget && (widget == this || isAncestorOf(widget))) {
    if (event->type() == QEvent::MouseMove || event->type() == QEvent::KeyPress ||
        event->type() == QEvent::FocusIn)
      revealControls();
    if (event->type() == QEvent::KeyPress && widget == list_) {
      auto* key = static_cast<QKeyEvent*>(event);
      if (key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace) {
        removeSelected();
        return true;
      }
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

bool PlayerWindow::saveScreenshot(const QString& path) {
  ensurePolished();
  if (layout())
    layout()->activate();
  QImage image(size() * devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
  image.setDevicePixelRatio(devicePixelRatioF());
  image.fill(Qt::transparent);
  render(&image);
  return image.save(path);
}
}  // namespace player_demo
