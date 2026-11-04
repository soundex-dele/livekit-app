#include "window.hpp"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QHash>
#include <QMouseEvent>
#include <QPair>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygon>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QTime>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>

static void initPlayerResources() {
  Q_INIT_RESOURCE(resources);
}

namespace player_demo {
QString formatClock(qint64 us) {
  const auto seconds = std::max<qint64>(0, us / 1000000);
  return QStringLiteral("%1:%2:%3")
      .arg(seconds / 3600, 2, 10, QChar('0'))
      .arg(seconds / 60 % 60, 2, 10, QChar('0'))
      .arg(seconds % 60, 2, 10, QChar('0'));
}

SeekSlider::SeekSlider(QWidget* parent) : QSlider(Qt::Horizontal, parent) {
  setMouseTracking(true);
}

int SeekSlider::valueAt(int x) const {
  const int handle = 12;
  return QStyle::sliderValueFromPosition(minimum(), maximum(), x - handle / 2,
                                        std::max(1, width() - handle));
}

void SeekSlider::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || !isEnabled()) {
    QSlider::mousePressEvent(event);
    return;
  }
  dragging_ = true;
  setSliderDown(true);
  const int value = valueAt(static_cast<int>(event->position().x()));
  setSliderPosition(value);
  emit sliderMoved(value);
}

void SeekSlider::mouseMoveEvent(QMouseEvent* event) {
  QSlider::mouseMoveEvent(event);
  const int value = valueAt(static_cast<int>(event->position().x()));
  if (durationUs_ > 0)
    QToolTip::showText(event->globalPosition().toPoint(),
                       formatClock(durationUs_ * value / std::max(1, maximum())), this);
  if (dragging_) {
    setSliderPosition(value);
    emit sliderMoved(value);
  }
}

void SeekSlider::mouseReleaseEvent(QMouseEvent* event) {
  if (!dragging_) {
    QSlider::mouseReleaseEvent(event);
    return;
  }
  dragging_ = false;
  setSliderDown(false);
  setValue(sliderPosition());
  emit sliderReleased();
}

namespace {
// PotPlayer-like thin-line glyphs, drawn at runtime so the UI never depends on
// the optional QtSvg image plugin. Coordinates use a 24x24 design grid.
void drawGlyph(QPainter& p, const QString& name) {
  const auto line = [&p](qreal x1, qreal y1, qreal x2, qreal y2) {
    p.drawLine(QPointF(x1, y1), QPointF(x2, y2));
  };
  const auto polygon = [&p](std::initializer_list<QPointF> points, bool filled) {
    QPolygonF shape;
    for (const auto& point : points)
      shape << point;
    if (filled)
      p.drawPolygon(shape);
    else
      p.drawPolyline(shape);
  };
  const auto solid = [&p] { p.setBrush(p.pen().color()); };
  if (name == "play") {
    solid();
    polygon({{8, 5}, {19, 12}, {8, 19}}, true);
  } else if (name == "pause") {
    solid();
    p.drawRoundedRect(QRectF(7, 5, 4, 14), 1, 1);
    p.drawRoundedRect(QRectF(14, 5, 4, 14), 1, 1);
  } else if (name == "stop") {
    solid();
    p.drawRoundedRect(QRectF(6, 6, 12, 12), 1.5, 1.5);
  } else if (name == "previous") {
    solid();
    polygon({{18, 5}, {8, 12}, {18, 19}}, true);
    line(6, 5, 6, 19);
  } else if (name == "next") {
    solid();
    polygon({{6, 5}, {16, 12}, {6, 19}}, true);
    line(18, 5, 18, 19);
  } else if (name == "volume") {
    polygon({{3, 9}, {7, 9}, {12, 5}, {12, 19}, {7, 15}, {3, 15}, {3, 9}}, false);
    p.drawArc(QRectF(13, 7.5, 7, 9), -65 * 16, 130 * 16);
    p.drawArc(QRectF(14.5, 5.5, 10, 13), -65 * 16, 130 * 16);
  } else if (name == "fullscreen") {
    line(4, 9, 4, 4);
    line(4, 4, 9, 4);
    line(15, 4, 20, 4);
    line(20, 4, 20, 9);
    line(20, 15, 20, 20);
    line(20, 20, 15, 20);
    line(9, 20, 4, 20);
    line(4, 20, 4, 15);
  } else if (name == "list") {
    for (int y : {7, 12, 17}) {
      p.drawEllipse(QPointF(5, y), 1.0, 1.0);
      line(9, y, 20, y);
    }
  } else if (name == "open") {
    polygon({{3, 7}, {9, 7}, {11, 10}, {20, 10}, {20, 19}, {3, 19}, {3, 7}}, false);
  } else if (name == "add") {
    line(12, 5, 12, 19);
    line(5, 12, 19, 12);
  } else if (name == "remove") {
    line(6, 12, 18, 12);
  } else if (name == "up") {
    line(12, 19, 12, 5);
    polygon({{7, 10}, {12, 5}, {17, 10}}, false);
  } else if (name == "down") {
    line(12, 5, 12, 19);
    polygon({{7, 14}, {12, 19}, {17, 14}}, false);
  } else if (name == "subtitle") {
    p.drawRoundedRect(QRectF(3, 5, 18, 14), 2, 2);
    line(6, 10, 11, 10);
    line(13, 10, 18, 10);
    line(6, 14, 10, 14);
    line(12, 14, 17, 14);
  } else if (name == "settings") {
    p.drawEllipse(QPointF(12, 12), 3.2, 3.2);
    static const qreal dirs[8][2] = {{1, 0},          {0.7071, 0.7071},  {0, 1},
                                     {-0.7071, 0.7071}, {-1, 0},          {-0.7071, -0.7071},
                                     {0, -1},          {0.7071, -0.7071}};
    for (const auto& direction : dirs)
      line(12 + direction[0] * 6, 12 + direction[1] * 6, 12 + direction[0] * 9,
           12 + direction[1] * 9);
  } else if (name == "sequential") {
    line(4, 6, 17, 6);
    line(4, 12, 17, 12);
    line(4, 18, 13, 18);
    polygon({{15, 15}, {19, 18}, {15, 21}}, false);
  } else if (name == "repeat" || name == "repeat-one") {
    p.drawArc(QRectF(4, 4, 16, 10), 20 * 16, 140 * 16);
    solid();
    polygon({{17, 4}, {21, 7}, {18, 10}}, true);
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(4, 10, 16, 10), 200 * 16, 140 * 16);
    solid();
    polygon({{7, 20}, {3, 17}, {6, 14}}, true);
    if (name == "repeat-one") {
      p.setBrush(Qt::NoBrush);
      line(11, 10.5, 13, 9.5);
      line(13, 9.5, 13, 15);
      line(11, 15, 15, 15);
    }
  }
}

QIcon icon(const QString& name) {
  static QHash<QString, QIcon> cache;
  const auto cached = cache.constFind(name);
  if (cached != cache.constEnd())
    return *cached;
  const auto makePixmap = [&name](const QColor& color) {
    QPixmap image(48, 48);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.translate(4, 4);
    painter.scale(40.0 / 24.0, 40.0 / 24.0);
    drawGlyph(painter, name);
    painter.end();
    return image;
  };
  QIcon value;
  value.addPixmap(makePixmap(QColor("#dce6f3")), QIcon::Normal);
  value.addPixmap(makePixmap(QColor("#6d7b8d")), QIcon::Disabled);
  cache.insert(name, value);
  return value;
}

QToolButton* button(QAction* action, QWidget* parent) {
  auto* value = new QToolButton(parent);
  value->setDefaultAction(action);
  value->setIconSize({18, 18});
  value->setMinimumSize(30, 28);
  value->setAccessibleName(action->text());
  return value;
}

const char* repeatIcon(RepeatMode mode) {
  switch (mode) {
    case RepeatMode::One:
      return "repeat-one";
    case RepeatMode::All:
      return "repeat";
    default:
      return "sequential";
  }
}

const QStringList& repeatModes() {
  static const QStringList modes{QStringLiteral("顺序播放"), QStringLiteral("单曲循环"),
                                 QStringLiteral("列表循环")};
  return modes;
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
  setWindowTitle(QStringLiteral("LiveKit Player"));
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
  connect(&idleTimer_, &QTimer::timeout, this, [this] {
    checkIdle();
    updateChrome();
  });
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
  QTimer::singleShot(0, this, [this] { positionOverlays(); });
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
    QMainWindow, QWidget#root, QWidget#videoArea { background: #0b0d10; color: #dce3ed; }
    QWidget { font-size: 12px; }
    QMenuBar { background: #151a20; color: #c9d4e2; border-bottom: 1px solid #232b35; }
    QMenuBar::item { padding: 5px 11px; background: transparent; }
    QMenuBar::item:selected { background: #2b3949; }
    QMenu { background: #1a2028; color: #d8e2ef; border: 1px solid #2e3844; padding: 4px; }
    QMenu::item { padding: 6px 26px 6px 18px; border-radius: 3px; }
    QMenu::item:selected { background: #2f6fb5; color: #fff; }
    QMenu::item:disabled { color: #6b7787; }
    QMenu::separator { height: 1px; background: #2b3440; margin: 4px 8px; }
    QDialog { background: #151a20; color: #dce3ed; }
    QLabel { color: #b9c6d6; background: transparent; }
    QLabel#banner { background: #3a2c20; color: #ffd9a3; padding: 7px 12px; }
    QWidget#topBar { background: rgba(9, 12, 16, 214); border-bottom: 1px solid #202832; }
    QWidget#controls { background: rgba(14, 18, 24, 238); border-top: 1px solid #2a333e; }
    QWidget#listPanel { background: #12171d; border-left: 1px solid #232b35; }
    QLabel#listHeader { color: #8fa2b8; font-size: 11px; }
    QLabel#topTitle { color: #e8eff9; font-size: 13px; }
    QLabel#time, QLabel#topClock { color: #dce6f3; font-family: Consolas, 'Courier New', monospace; font-size: 12px; }
    QLabel#status { color: #8797aa; font-size: 11px; }
    QToolButton, QPushButton { color: #dce3ed; background: transparent; border: 1px solid transparent; border-radius: 3px; padding: 3px; }
    QToolButton:hover, QPushButton:hover { background: #2c3a4b; }
    QToolButton:pressed, QToolButton:checked { background: #2f6fb5; }
    QToolButton:focus, QPushButton:focus, QListView:focus, QSlider:focus { border: 1px solid #6ea8f0; }
    QToolButton:disabled, QPushButton:disabled { color: #5f6b7b; }
    QSlider { border: 1px solid transparent; background: transparent; }
    QSlider::groove:horizontal { background: #39424f; height: 4px; border-radius: 2px; }
    QSlider::sub-page:horizontal { background: #4f9cf5; border-radius: 2px; }
    QSlider::add-page:horizontal { background: #39424f; border-radius: 2px; }
    QSlider::handle:horizontal { background: #e6f0ff; width: 10px; height: 10px; margin: -4px 0; border-radius: 5px; }
    QSlider::handle:horizontal:hover { background: #ffffff; width: 12px; margin: -5px 0; border-radius: 6px; }
    QSlider#progress::groove:horizontal { height: 5px; }
    QSlider#volume::groove:horizontal { height: 3px; }
    QListView { background: #12171d; color: #c4d1e0; border: 1px solid #202832; outline: 0; }
    QListView::item { padding: 6px 7px; border-radius: 2px; }
    QListView::item:selected { background: #2f6fb5; color: #fff; }
    QListView::item:hover { background: #222c38; }
    QSplitter::handle { background: #232b35; width: 1px; }
    QSpinBox { color: #dce3ed; background: #0f141a; padding: 5px; border: 1px solid #2b3440; }
    QCheckBox { color: #dce3ed; padding: 5px; }
    QToolTip { background: #222c38; color: #f0f6ff; border: 1px solid #3c4a5c; padding: 3px 6px; }
  )");
  repeat_ = static_cast<RepeatMode>(std::clamp(settings_.value("repeat", 0).toInt(), 0, 2));
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
  repeatCycle_ = action("cycleRepeat", QStringLiteral("播放模式"), repeatIcon(repeat_));
  repeatCycle_->setToolTip(repeatModes()[static_cast<int>(repeat_)] +
                           QStringLiteral("（点击切换）"));
  auto* subtitle = action("loadSubtitle", QStringLiteral("加载字幕…"), "subtitle");
  subtitleToggle_ = action("subtitleVisible", QStringLiteral("显示字幕"), {}, Qt::Key_V);
  subtitleToggle_->setCheckable(true);
  subtitleToggle_->setChecked(true);
  auto* subSettings = action("subtitleSettings", QStringLiteral("字幕设置…"));
  auto* preferences =
      action("settings", QStringLiteral("选项…"), "settings", QKeySequence::Preferences);
  auto* shortcuts = action("shortcuts", QStringLiteral("快捷键…"), {}, QKeySequence::HelpContents);
  auto* remove = action("remove", QStringLiteral("删除所选"), "remove");
  auto* clear = action("clear", QStringLiteral("清空列表"));
  auto* up = action("moveUp", QStringLiteral("向上移动"), "up");
  auto* down = action("moveDown", QStringLiteral("向下移动"), "down");
  auto* quit = action("quit", QStringLiteral("退出"), {}, QKeySequence::Quit);
  auto* escape = action("escape", QStringLiteral("退出全屏"), {}, Qt::Key_Escape);
  connect(open, &QAction::triggered, this, [this] { browse(false); });
  connect(add, &QAction::triggered, this, [this] { browse(true); });
  connect(play_, &QAction::triggered, this, [this] {
    const bool wasPlaying = controller_.snapshot().state == sdk::PlayerState::playing;
    if ((controller_.snapshot().state == sdk::PlayerState::closed ||
         !controller_.error().isEmpty()) &&
        playlist_.rowCount())
      playRow(std::max(0, playlist_.rowForId(playlist_.currentId())));
    else
      controller_.togglePlay();
    video_->flash(wasPlaying ? FlashKind::pause : FlashKind::play);
    video_->showOsd(wasPlaying ? QStringLiteral("暂停") : QStringLiteral("播放"));
  });
  connect(stop_, &QAction::triggered, this, [this] {
    controller_.stop();
    video_->flash(FlashKind::stop);
    video_->showOsd(QStringLiteral("停止"));
  });
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
  connect(mute_, &QAction::toggled, this, [this](bool muted) {
    updateVolume();
    video_->showOsd(muted ? QStringLiteral("静音") : QStringLiteral("取消静音"));
  });
  connect(fullscreen_, &QAction::triggered, this, &PlayerWindow::toggleFullscreen);
  connect(escape, &QAction::triggered, this, [this] {
    if (isFullScreen())
      toggleFullscreen();
  });
  connect(listToggle_, &QAction::toggled, this, [this](bool visible) {
    updateListVisibility();
    video_->showOsd(visible ? QStringLiteral("播放列表：显示") : QStringLiteral("播放列表：隐藏"));
  });
  connect(repeatCycle_, &QAction::triggered, this, &PlayerWindow::cycleRepeat);
  connect(subtitle, &QAction::triggered, this, &PlayerWindow::browseSubtitles);
  connect(subtitleToggle_, &QAction::toggled, this, [this](bool enabled) {
    controller_.subtitles.enabled = enabled;
    video_->showOsd(enabled ? QStringLiteral("字幕已显示") : QStringLiteral("字幕已隐藏"));
  });
  connect(subSettings, &QAction::triggered, this, &PlayerWindow::subtitleSettings);
  connect(preferences, &QAction::triggered, this, &PlayerWindow::showSettings);
  connect(shortcuts, &QAction::triggered, this, &PlayerWindow::showShortcuts);
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
  fileMenu->addActions({open, add});
  fileMenu->addSeparator();
  fileMenu->addAction(quit);
  auto* playbackMenu = menuBar()->addMenu(QStringLiteral("播放"));
  playbackMenu->addActions({play_, stop_, previous_, next_, back, forward});
  auto* repeatMenu = playbackMenu->addMenu(QStringLiteral("播放模式"));
  auto* group = new QActionGroup(this);
  for (int i = 0; i < repeatModes().size(); ++i) {
    auto* value = action(qPrintable("repeat" + QString::number(i)), repeatModes()[i]);
    value->setCheckable(true);
    group->addAction(value);
    repeatMenu->addAction(value);
    repeatActions_.append(value);
    value->setChecked(i == static_cast<int>(repeat_));
    connect(value, &QAction::triggered, this, [this, i] {
      repeat_ = static_cast<RepeatMode>(i);
      repeatCycle_->setIcon(icon(repeatIcon(repeat_)));
      repeatCycle_->setToolTip(repeatModes()[i] + QStringLiteral("（点击切换）"));
      refresh();
    });
  }
  auto* audioMenu = menuBar()->addMenu(QStringLiteral("声音"));
  audioMenu->addAction(mute_);
  auto* subMenu = menuBar()->addMenu(QStringLiteral("字幕"));
  subMenu->addActions({subtitle, subtitleToggle_, subSettings});
  auto* viewMenu = menuBar()->addMenu(QStringLiteral("视图"));
  viewMenu->addActions({fullscreen_, listToggle_});
  auto* optionsMenu = menuBar()->addMenu(QStringLiteral("选项"));
  optionsMenu->addActions({preferences, subSettings});
  auto* helpMenu = menuBar()->addMenu(QStringLiteral("帮助"));
  helpMenu->addAction(shortcuts);
  contextMenu_ = new QMenu(this);
  contextMenu_->setObjectName("contextMenu");
  auto* fileContext = contextMenu_->addMenu(QStringLiteral("文件"));
  fileContext->addActions({open, add});
  contextMenu_->addSeparator();
  contextMenu_->addActions({play_, stop_});
  auto* playbackContext = contextMenu_->addMenu(QStringLiteral("播放"));
  playbackContext->addActions({previous_, next_, back, forward});
  playbackContext->addMenu(repeatMenu);
  contextMenu_->addMenu(subMenu);
  auto* audioContext = contextMenu_->addMenu(QStringLiteral("声音"));
  audioContext->addAction(mute_);
  auto* viewContext = contextMenu_->addMenu(QStringLiteral("视图"));
  viewContext->addActions({fullscreen_, listToggle_});
  contextMenu_->addSeparator();
  contextMenu_->addActions({preferences, shortcuts, quit});

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

  videoArea_ = new QWidget(splitter_);
  videoArea_->setObjectName("videoArea");
  videoArea_->setMinimumSize(200, 180);
  video_ = new VideoWidget(videoArea_);
  video_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(video_, &QWidget::customContextMenuRequested, this, [this](const QPoint& p) {
    revealControls();
    contextMenu_->popup(video_->mapToGlobal(p));
  });

  topBar_ = new QWidget(videoArea_);
  topBar_->setObjectName("topBar");
  auto* topLayout = new QHBoxLayout(topBar_);
  topLayout->setContentsMargins(14, 8, 14, 8);
  topTitle_ = new QLabel(QStringLiteral("LiveKit Player"), topBar_);
  topTitle_->setObjectName("topTitle");
  topClock_ = new QLabel("00:00:00 / 00:00:00", topBar_);
  topClock_->setObjectName("topClock");
  topLayout->addWidget(topTitle_, 1);
  topLayout->addWidget(topClock_);
  topBar_->hide();

  controls_ = new QWidget(videoArea_);
  controls_->setObjectName("controls");
  auto* controlLayout = new QVBoxLayout(controls_);
  controlLayout->setContentsMargins(14, 7, 14, 9);
  controlLayout->setSpacing(5);
  auto* seekRow = new QHBoxLayout;
  seekRow->setSpacing(10);
  progress_ = new SeekSlider(controls_);
  progress_->setObjectName("progress");
  progress_->setAccessibleName(QStringLiteral("播放进度"));
  progress_->setRange(0, 10000);
  progress_->setFixedHeight(16);
  time_ = new QLabel("00:00:00 / 00:00:00", controls_);
  time_->setObjectName("time");
  time_->setMinimumWidth(150);
  time_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  seekRow->addWidget(progress_, 1);
  seekRow->addWidget(time_);
  controlLayout->addLayout(seekRow);
  connect(progress_, &QSlider::sliderMoved, this, [this](int value) {
    const qint64 duration = controller_.snapshot().media.duration_us;
    const qint64 position = duration * value / 10000;
    time_->setText(formatClock(position) + " / " + formatClock(duration));
    progress_->setToolTip(formatClock(position));
  });
  connect(progress_, &QSlider::sliderReleased, this, [this] {
    const qint64 position = controller_.snapshot().media.duration_us * progress_->value() / 10000;
    controller_.seek(position);
    video_->showOsd(QStringLiteral("定位至 ") + formatClock(position));
  });
  connect(progress_, &QSlider::actionTriggered, this, [this](int triggered) {
    if (triggered != QAbstractSlider::SliderMove && !progress_->isSliderDown())
      controller_.seek(controller_.snapshot().media.duration_us * progress_->sliderPosition() /
                       10000);
  });
  auto* transport = new QHBoxLayout;
  transport->setSpacing(3);
  for (auto* value : {open, previous_, play_, stop_, next_}) {
    auto* item = button(value, controls_);
    if (value == open)
      optionalButtons_.append(item);
    transport->addWidget(item);
  }
  transport->addSpacing(8);
  transport->addWidget(button(mute_, controls_));
  volume_ = new QSlider(Qt::Horizontal, controls_);
  volume_->setObjectName("volume");
  volume_->setAccessibleName(QStringLiteral("音量"));
  volume_->setFixedWidth(92);
  volume_->setRange(0, 100);
  volume_->setValue(std::clamp(settings_.value("volume", 80).toInt(), 0, 100));
  connect(volume_, &QSlider::valueChanged, this, [this](int value) {
    updateVolume();
    video_->showOsd(QStringLiteral("音量 %1%").arg(value));
  });
  transport->addWidget(volume_);
  transport->addSpacing(10);
  status_ = new QLabel(QStringLiteral("就绪"), controls_);
  status_->setObjectName("status");
  transport->addWidget(status_);
  transport->addStretch(1);
  for (auto* value : {repeatCycle_, subtitle, listToggle_, fullscreen_, preferences}) {
    auto* item = button(value, controls_);
    if (value == subtitle)
      optionalButtons_.append(item);
    transport->addWidget(item);
  }
  controlLayout->addLayout(transport);

  listPanel_ = new QWidget(splitter_);
  listPanel_->setObjectName("listPanel");
  listPanel_->setMinimumWidth(200);
  auto* listLayout = new QVBoxLayout(listPanel_);
  listLayout->setContentsMargins(10, 10, 10, 8);
  listLayout->setSpacing(6);
  count_ = new QLabel(QStringLiteral("播放列表"), listPanel_);
  count_->setObjectName("listHeader");
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

  subtitleSize_ = std::clamp(settings_.value("subtitleSize", 28).toInt(), 16, 64);
  {
    QSignalBlocker block(mute_);
    mute_->setChecked(settings_.value("muted", false).toBool());
  }
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

void PlayerWindow::showShortcuts() {
  QDialog dialog(this);
  dialog.setWindowTitle(QStringLiteral("快捷键"));
  auto* form = new QFormLayout(&dialog);
  const QVector<QPair<QString, QString>> entries{
      {QStringLiteral("空格"), QStringLiteral("播放 / 暂停")},
      {QStringLiteral("S"), QStringLiteral("停止")},
      {QStringLiteral("← / →"), QStringLiteral("后退 / 前进 5 秒")},
      {QStringLiteral("Ctrl+← / Ctrl+→"), QStringLiteral("上一集 / 下一集")},
      {QStringLiteral("M"), QStringLiteral("静音")},
      {QStringLiteral("鼠标滚轮"), QStringLiteral("调节音量")},
      {QStringLiteral("F / 双击画面"), QStringLiteral("全屏切换")},
      {QStringLiteral("V"), QStringLiteral("显示 / 隐藏字幕")},
      {QStringLiteral("F6"), QStringLiteral("显示 / 隐藏播放列表")},
      {QStringLiteral("Ctrl+O"), QStringLiteral("打开文件")},
      {QStringLiteral("Esc"), QStringLiteral("退出全屏")}};
  for (const auto& entry : entries)
    form->addRow(entry.first, new QLabel(entry.second, &dialog));
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  dialog.exec();
}

void PlayerWindow::updateVolume() {
  if (!volume_)
    return;
  controller_.setVolume(volume_->value(), mute_->isChecked());
  volume_->setToolTip(QString::number(volume_->value()) + "%");
}

void PlayerWindow::adjustVolume(int delta) {
  const int value = std::clamp(volume_->value() + delta, 0, 100);
  volume_->setValue(value);
  video_->showOsd(QStringLiteral("音量 %1%").arg(value));
}

void PlayerWindow::cycleRepeat() {
  repeat_ = static_cast<RepeatMode>((static_cast<int>(repeat_) + 1) % repeatModes().size());
  const int index = static_cast<int>(repeat_);
  repeatActions_[index]->setChecked(true);
  repeatCycle_->setIcon(icon(repeatIcon(repeat_)));
  repeatCycle_->setToolTip(repeatModes()[index] + QStringLiteral("（点击切换）"));
  video_->showOsd(repeatModes()[index]);
  refresh();
}

void PlayerWindow::updateStatus(const sdk::PlayerSnapshot& state) {
  if (!status_)
    return;
  QStringList parts;
  if (state.media.has_video && state.media.width > 0 && state.media.height > 0)
    parts << QStringLiteral("%1×%2").arg(state.media.width).arg(state.media.height);
  if (state.media.has_audio)
    parts << QStringLiteral("%1 kHz · %2 ch")
                 .arg(state.media.audio_sample_rate / 1000.0, 0, 'f', 1)
                 .arg(state.media.audio_channels);
  const QString text = parts.isEmpty() ? QStringLiteral("就绪")
                                       : parts.join(QStringLiteral("   ·   "));
  if (status_->text() != text)
    status_->setText(text);
}

void PlayerWindow::updateChrome() {
  if (!topBar_ || !controls_)
    return;
  const bool visible = isFullScreen() && !controls_->isHidden();
  topBar_->setVisible(visible);
  if (!visible)
    return;
  topTitle_->setText(controller_.path().isEmpty()
                         ? QStringLiteral("LiveKit Player")
                         : QFileInfo(controller_.path()).fileName());
  const auto state = controller_.snapshot();
  topClock_->setText(formatClock(state.position_us) + " / " +
                     formatClock(state.media.duration_us) + "     " +
                     QTime::currentTime().toString(QStringLiteral("HH:mm")));
}

void PlayerWindow::positionOverlays() {
  if (!videoArea_ || !video_ || !controls_ || !topBar_)
    return;
  const QSize area = videoArea_->size();
  // PotPlayer drops secondary chrome when the video pane gets narrow.
  const bool compact = area.width() < 640;
  status_->setVisible(!compact);
  volume_->setFixedWidth(compact ? 60 : 92);
  for (auto* item : optionalButtons_)
    item->setVisible(!compact);
  controls_->ensurePolished();
  if (auto* controlLayout = controls_->layout())
    controlLayout->activate();
  video_->setGeometry(0, 0, area.width(), area.height());
  const int controlHeight = std::max(60, controls_->sizeHint().height());
  controls_->setGeometry(0, area.height() - controlHeight, area.width(), controlHeight);
  topBar_->setGeometry(0, 0, area.width(), std::max(30, topBar_->sizeHint().height()));
  video_->setBottomInset(controlHeight + 12);
  controls_->raise();
  topBar_->raise();
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
  progress_->setDuration(state.media.duration_us);
  progress_->setEnabled(loaded && state.media.duration_us > 0 && !exiting_);
  if (!progress_->isSliderDown()) {
    QSignalBlocker block(progress_);
    progress_->setValue(
        state.media.duration_us > 0 ? state.position_us * 10000 / state.media.duration_us : 0);
  }
  if (!progress_->isSliderDown())
    time_->setText(formatClock(state.position_us) + " / " + formatClock(state.media.duration_us));
  updateStatus(state);
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
  count_->setText(QStringLiteral("播放列表  ·  %1 项").arg(playlist_.rowCount()));
  const QString title =
      controller_.path().isEmpty()
          ? QStringLiteral("LiveKit Player")
          : QFileInfo(controller_.path()).fileName() + QStringLiteral(" — LiveKit Player");
  setWindowTitle(title);
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
  QTimer::singleShot(0, this, [this] { positionOverlays(); });
}

void PlayerWindow::updateListVisibility() {
  if (!listPanel_)
    return;
  const bool visible = listToggle_->isChecked() && !isFullScreen();
  if (visible) {
    listPanel_->setMinimumWidth(200);
    listPanel_->setMaximumWidth(QWIDGETSIZE_MAX);
  } else {
    if (listPanel_->width() > 0)
      listWidth_ = listPanel_->width();
    // A hidden QSplitter pane otherwise keeps its width and leaves dead space.
    listPanel_->setMinimumWidth(0);
    listPanel_->setMaximumWidth(0);
  }
  listPanel_->setVisible(visible);
  const int total = splitter_->width();
  if (total > 0) {
    if (visible && listWidth_ > 0)
      splitter_->setSizes({std::max(200, total - listWidth_), listWidth_});
    else if (!visible)
      splitter_->setSizes({total, 0});
  }
  QTimer::singleShot(0, this, [this] { positionOverlays(); });
}

void PlayerWindow::revealControls() {
  idle_.restart();
  controls_->show();
  updateChrome();
  video_->unsetCursor();
}

void PlayerWindow::checkIdle() {
  if (!isFullScreen() || idle_.elapsed() < 3000 || QApplication::activePopupWidget() ||
      QApplication::activeModalWidget() || progress_->isSliderDown() || volume_->isSliderDown())
    return;
  if (auto* focus = QApplication::focusWidget(); focus && controls_->isAncestorOf(focus))
    return;
  controls_->hide();
  topBar_->hide();
  video_->setCursor(Qt::BlankCursor);
}

bool PlayerWindow::eventFilter(QObject* watched, QEvent* event) {
  auto* widget = qobject_cast<QWidget*>(watched);
  if (widget == videoArea_ && event->type() == QEvent::Resize)
    positionOverlays();
  if (widget && (widget == this || isAncestorOf(widget))) {
    if (event->type() == QEvent::MouseMove || event->type() == QEvent::KeyPress ||
        event->type() == QEvent::FocusIn)
      revealControls();
    if (widget == video_ && event->type() == QEvent::Wheel) {
      const auto delta = static_cast<QWheelEvent*>(event)->angleDelta().y();
      if (delta != 0)
        adjustVolume(delta > 0 ? 5 : -5);
      revealControls();
      return true;
    }
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
  positionOverlays();
  if (layout())
    layout()->activate();
  QImage image(size() * devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
  image.setDevicePixelRatio(devicePixelRatioF());
  image.fill(Qt::transparent);
  render(&image);
  return image.save(path);
}
}  // namespace player_demo
