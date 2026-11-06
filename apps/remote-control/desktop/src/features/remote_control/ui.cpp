#include "ui.hpp"
#include <av/ui.hpp>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QRegularExpressionValidator>
#include <QResizeEvent>
#include <QSettings>
#include <QSize>
#include <QStackedWidget>
#include <QStyle>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

namespace remote_app {
namespace {
struct NoInput : sdk::ControlInputSink {
  bool apply(const sdk::ControlInput&) noexcept override {
    return true;
  }
};

QLabel* label(QString text, QWidget* parent = nullptr) {
  auto l = new QLabel(text, parent);
  l->setWordWrap(true);
  l->setTextFormat(Qt::PlainText);
  return l;
}

QPushButton* button(QString text, QString name, bool primary = false) {
  auto b = new QPushButton(text);
  b->setObjectName(name);
  b->setMinimumHeight(38);
  b->setCursor(Qt::PointingHandCursor);
  av::ui::set_button_role(b, primary ? av::ui::ButtonRole::Primary : av::ui::ButtonRole::Secondary);
  return b;
}

QWidget* card(QString title, QVBoxLayout*& layout) {
  auto w = new av::ui::Card;
  w->setObjectName("card");
  layout = new QVBoxLayout(w);
  layout->setContentsMargins(24, 24, 24, 24);
  layout->setSpacing(16);
  auto t = label(title);
  t->setObjectName("cardTitle");
  av::ui::set_text_role(t, "heading");
  layout->addWidget(t);
  return w;
}

QString state_label(const std::string& v) {
  if (v == "waiting")
    return "等待连接";
  if (v == "pending")
    return "等待本机确认";
  if (v == "approved")
    return "正在共享本机 · 可随时停止";
  if (v == "creating")
    return "正在创建连接码…";
  if (v == "requesting")
    return "正在申请连接…";
  if (v == "connecting")
    return "正在建立安全会话…";
  if (v == "connected")
    return "远程连接已建立";
  if (v == "ended")
    return "连接已结束";
  return "准备就绪";
}

QIcon app_icon(bool active = false) {
  QPixmap pix(64, 64);
  pix.fill(Qt::transparent);
  QPainter p(&pix);
  p.setRenderHint(QPainter::Antialiasing);
  p.setBrush(QColor(active ? "#dc2626" : "#2563eb"));
  p.setPen(Qt::NoPen);
  p.drawRoundedRect(2, 2, 60, 60, 14, 14);
  p.setPen(QPen(Qt::white, 3));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(13, 15, 38, 27, 3, 3);
  p.drawLine(24, 50, 40, 50);
  p.drawLine(32, 42, 32, 50);
  return QIcon(pix);
}

QIcon nav_icon(char kind) {
  const auto glyph = kind == 'm'   ? av::ui::Icon::Screen
                     : kind == 's' ? av::ui::Icon::Settings
                                   : av::ui::Icon::Info;
  return av::ui::icon(glyph, QColor("#2563EB"));
}

void window_theme(QWidget* window) {
  av::ui::apply_theme(window, av::ui::Theme::Light);
  window->setStyleSheet(window->styleSheet() + QStringLiteral(R"(
    QFrame#topbar { border-bottom: 1px solid #DFE5EF; }
    QFrame#composer { border-top: 1px solid #DFE5EF; }
    QLineEdit#localCode { font-size: 28px; font-weight: 700; padding: 12px;
      color: #1D4ED8; background: #EFF6FF; border-color: #BFDBFE; }
    QPushButton[nav="true"] { text-align: left; background: transparent;
      border: 1px solid transparent; padding: 10px 12px; }
    QPushButton[nav="true"]:checked { color: #1D4ED8; background: #EFF6FF;
      border-color: #BFDBFE; font-weight: 600; }
    QScrollArea#taskScroll { background: transparent; border: 0; }
    QWidget#taskViewport, QWidget#connectionTasks { background: #F5F7FB; }
    QScrollArea#remoteScroll { background: #0B1220; border: 0; }
    QToolBar#sessionToolbar { spacing: 8px; padding: 8px 12px;
      border-bottom: 1px solid #DFE5EF; }
    QToolBar#sessionToolbar::separator { width: 1px; background: #D5DEEB;
      margin: 8px 4px; }
    QFrame#approvalCard { background: #EFF6FF; border: 1px solid #93C5FD;
      border-radius: 12px; }
  )"));
}
}  // namespace

void apply_theme() {
  qApp->setWindowIcon(app_icon());
}

ControlWindow::ControlWindow(Controller& controller, QWidget* parent)
    : QMainWindow(parent), controller_(controller) {
  window_theme(this);
  setWindowFlag(Qt::Window);
  setWindowTitle("远程桌面 — AV Remote");
  resize(1180, 780);
  setMinimumSize(720, 520);
  auto toolbar = addToolBar("会话操作");
  toolbar->setMovable(false);
  toolbar->setObjectName("sessionToolbar");
  av::ui::set_surface(toolbar, "toolbar");
  toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  auto fit = toolbar->addAction("适应窗口");
  auto original = toolbar->addAction("原始比例");
  auto full = toolbar->addAction("全屏");
  auto pause = toolbar->addAction("暂停输入");
  toolbar->addSeparator();
  auto disconnect =
      toolbar->addAction(av::ui::icon(av::ui::Icon::Stop, QColor("#DC2626")), "断开连接");
  auto center = new QFrame;
  center->setObjectName("viewer");
  av::ui::set_surface(center, "media");
  center->setFrameShape(QFrame::NoFrame);
  auto layout = new QVBoxLayout(center);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  scroll_ = new QScrollArea;
  scroll_->setObjectName("remoteScroll");
  scroll_->setWidgetResizable(true);
  video_ = new VideoWidget;
  scroll_->setWidget(video_);
  layout->addWidget(scroll_, 1);
  auto composer = new QFrame;
  composer->setObjectName("composer");
  av::ui::set_surface(composer, "toolbar");
  auto composerLayout = new QVBoxLayout(composer);
  composerLayout->setContentsMargins(16, 12, 16, 14);
  composerLayout->setSpacing(10);
  status_ = label("点击远程画面选择目标，再发送文字。离开窗口会暂停输入。");
  status_->setObjectName("textStatus");
  av::ui::set_text_role(status_, "muted");
  composerLayout->addWidget(status_);
  auto row = new QHBoxLayout;
  text_ = new QTextEdit;
  text_->setAccessibleName("发送到远端的文字");
  text_->setPlaceholderText("输入文字后点击发送，最多 256 个字符。Enter 为换行。");
  text_->setMaximumHeight(86);
  text_->setObjectName("remoteText");
  send_ = button("发送文字", "sendText", true);
  row->addWidget(text_, 1);
  row->addWidget(send_);
  composerLayout->addLayout(row);
  layout->addWidget(composer);
  setCentralWidget(center);
  video_->send = [this](sdk::Json e) { controller_.input(std::move(e)); };
  video_->released = [this] { controller_.release(); };
  video_->target_selected = [this] {
    send_->setEnabled(!text_->toPlainText().isEmpty() && controller_.snapshot().text_supported);
  };
  connect(fit, &QAction::triggered, this, [this] {
    video_->set_original(false);
    scroll_->setWidgetResizable(true);
  });
  connect(original, &QAction::triggered, this, [this] {
    scroll_->setWidgetResizable(false);
    video_->set_original(true);
  });
  connect(full, &QAction::triggered, this, [this] {
    video_->pause();
    isFullScreen() ? showNormal() : showFullScreen();
  });
  connect(pause, &QAction::triggered, this, [this] {
    video_->pause();
    status_->setText("键鼠已暂停，点击远程画面重新启用。");
  });
  connect(disconnect, &QAction::triggered, this, &QWidget::close);
  connect(send_, &QPushButton::clicked, this, [this] {
    try {
      if (!video_->target())
        return;
      controller_.send_text(text_->toPlainText().toUtf8().toStdString());
      present(controller_.snapshot());
    } catch (const std::exception& e) {
      status_->setText(QString::fromUtf8(e.what()));
    }
  });
  connect(text_, &QTextEdit::textChanged, this, [this] {
    auto s = controller_.snapshot();
    send_->setEnabled(!s.finished && s.text_supported && !s.text_pending && video_->target() &&
                      !text_->toPlainText().isEmpty());
  });
}

void ControlWindow::present(const ControllerSnapshot& s) {
  video_->present(s.image, !s.finished && s.layout > 0 && !s.image.isNull(), s.text_pending,
                  s.layout);
  if (s.text_accepted > accepted_) {
    accepted_ = s.text_accepted;
    text_->clear();
  }
  if (s.finished && !finished_) {
    text_->clear();
    video_->pause();
  }
  finished_ = s.finished;
  text_->setEnabled(!s.finished && s.text_supported && !s.text_pending);
  send_->setEnabled(text_->isEnabled() && video_->target() && !text_->toPlainText().isEmpty());
  send_->setText(s.text_pending ? "等待确认…" : "发送文字");
  if (!s.error.empty())
    status_->setText(QString::fromStdString(s.error));
  else if (!s.text_status.empty())
    status_->setText(QString::fromStdString(s.text_status));
  else if (s.finished)
    status_->setText("会话已结束，请重新申请连接。");
  else if (!s.text_supported)
    status_->setText("等待远端文字能力确认 · 点击画面启用键鼠");
}

void ControlWindow::closeEvent(QCloseEvent* e) {
  video_->pause();
  text_->clear();
  controller_.stop();
  if (stopped)
    stopped();
  e->accept();
}

void ControlWindow::changeEvent(QEvent* e) {
  if (e->type() == QEvent::ActivationChange && !isActiveWindow())
    video_->pause();
  QMainWindow::changeEvent(e);
}

MainWindow::MainWindow(bool test)
    : test_(test), host_(test, test ? std::make_shared<NoInput>() : nullptr) {
  window_theme(this);
  setWindowTitle("AV Remote · 远程协助");
  resize(1160, 780);
  setMinimumSize(960, 700);
  setWindowIcon(app_icon());
  auto shell = new QWidget;
  auto outer = new QVBoxLayout(shell);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);
  auto topbar = new QFrame;
  topbar->setObjectName("topbar");
  av::ui::set_surface(topbar, "toolbar");
  topbar->setFixedHeight(72);
  auto top = new QHBoxLayout(topbar);
  top->setContentsMargins(24, 0, 24, 0);
  auto topTitle = label("AV Remote  /  远程协助");
  topTitle->setObjectName("topTitle");
  av::ui::set_text_role(topTitle, "heading");
  top->addWidget(topTitle);
  top->addStretch();
  status_ = new av::ui::StatusBadge("准备就绪");
  status_->setObjectName("connectionStatus");
  top->addWidget(status_, 0, Qt::AlignVCenter);
  top->addSpacing(12);
  stop_ = button("停止当前会话", "stopSession");
  av::ui::set_button_role(stop_, av::ui::ButtonRole::Danger);
  top->addWidget(stop_);
  outer->addWidget(topbar);
  auto horizontal = new QHBoxLayout;
  horizontal->setContentsMargins(0, 0, 0, 0);
  horizontal->setSpacing(0);
  outer->addLayout(horizontal, 1);
  auto side = new QFrame;
  side->setObjectName("sidebar");
  side->setFrameShape(QFrame::NoFrame);
  side->setFixedWidth(192);
  sidebar_ = side;
  av::ui::set_surface(side, "sidebar");
  auto nav = new QVBoxLayout(side);
  nav->setContentsMargins(12, 24, 12, 20);
  nav->setSpacing(8);
  auto brandRow = new QHBoxLayout;
  brandRow->setSpacing(10);
  auto logo = new QLabel;
  logo->setPixmap(app_icon().pixmap(QSize(38, 38)));
  logo->setFixedSize(38, 38);
  brandRow->addWidget(logo);
  auto brandText = new QVBoxLayout;
  brandText->setSpacing(2);
  auto brandName = label("AV Remote");
  brandName->setObjectName("brandText");
  av::ui::set_text_role(brandName, "heading");
  auto brandSub = label("安全连接 · 需对方确认");
  brandSub->setObjectName("brandSub");
  av::ui::set_text_role(brandSub, "caption");
  brandText->addWidget(brandName);
  brandText->addWidget(brandSub);
  brandRow->addLayout(brandText);
  nav->addLayout(brandRow);
  nav->addSpacing(28);
  auto caption = label("工作台");
  caption->setObjectName("sideCaption");
  av::ui::set_text_role(caption, "caption");
  nav->addWidget(caption);
  auto pages = new QStackedWidget;
  auto navigation = new QButtonGroup(this);
  navigation->setExclusive(true);
  const char icons[] = {'m', 's', 'i'};
  for (auto title : {"远程控制", "设置", "关于"}) {
    int index = pages->count();
    auto b = button(title, QString("nav") + title);
    b->setProperty("nav", true);
    b->setProperty("navTitle", QString::fromUtf8(title));
    b->setToolTip(QString::fromUtf8(title));
    b->setAccessibleName(QString::fromUtf8(title));
    b->setIcon(nav_icon(icons[index]));
    b->setIconSize(QSize(20, 20));
    b->setMinimumHeight(44);
    b->setCheckable(true);
    navigation->addButton(b, index);
    b->setChecked(index == 0);
    auto page = new QWidget;
    pages->addWidget(page);
    connect(b, &QPushButton::clicked, this, [pages, index] { pages->setCurrentIndex(index); });
    nav->addWidget(b);
  }
  nav->addStretch();
  auto note = label("局域网协助\n每次连接均需确认");
  note->setObjectName("sideNote");
  av::ui::set_text_role(note, "caption");
  nav->addWidget(note);
  horizontal->addWidget(side);
  horizontal->addWidget(pages, 1);
  setCentralWidget(shell);
  auto home = new QVBoxLayout(pages->widget(0));
  home->setContentsMargins(24, 24, 24, 24);
  home->setSpacing(14);
  auto title = label("远程协助");
  title->setObjectName("title");
  av::ui::set_text_role(title, "title");
  home->addWidget(title);
  auto intro = label("输入对方连接码发起协助，或生成本机连接码接受协助。");
  intro->setObjectName("subtitle");
  av::ui::set_text_role(intro, "muted");
  home->addWidget(intro);
  home->addSpacing(4);
  auto taskScroll = new QScrollArea;
  taskScroll->setObjectName("taskScroll");
  taskScroll->setWidgetResizable(true);
  taskScroll->setFrameShape(QFrame::NoFrame);
  taskScroll->viewport()->setObjectName("taskViewport");
  taskScroll->viewport()->setAttribute(Qt::WA_StyledBackground);
  auto tasks = new QWidget;
  tasks->setObjectName("connectionTasks");
  tasks->setAttribute(Qt::WA_StyledBackground);
  auto cards = new QHBoxLayout(tasks);
  cards->setContentsMargins(0, 0, 0, 0);
  taskScroll->setWidget(tasks);
  cards->setSpacing(18);
  QVBoxLayout *local, *remote;
  auto localCard = card("让他人协助我", local);
  localCard->setObjectName("localCard");
  auto remoteCard = card("连接远程设备", remote);
  cards->addWidget(localCard, 1);
  cards->addWidget(remoteCard, 1);
  home->addWidget(taskScroll, 1);
  auto codeLabel = label("本机连接码");
  codeLabel->setObjectName("localCodeLabel");
  av::ui::set_text_role(codeLabel, "muted");
  local->addWidget(codeLabel);
  code_ = new QLineEdit;
  code_->setReadOnly(true);
  code_->setObjectName("localCode");
  code_->setPlaceholderText("尚未生成");
  codeLabel->setBuddy(code_);
  local->addWidget(code_);
  expiry_ = label("10 位连接码 · 5 分钟有效 · 单次使用");
  expiry_->setObjectName("localHint");
  av::ui::set_text_role(expiry_, "caption");
  local->addWidget(expiry_);
  copy_ = button("复制连接码", "copyCode");
  create_ = button("生成连接码", "createCode", true);
  local->addWidget(create_);
  local->addWidget(copy_);
  local->addStretch();
  auto peerLabel = label("对方连接码");
  remote->addWidget(peerLabel);
  remote_code_ = new QLineEdit;
  remote_code_->setObjectName("remoteCode");
  remote_code_->setPlaceholderText("请输入对方的 10 位连接码");
  remote_code_->setMaxLength(10);
  remote_code_->setValidator(
      new QRegularExpressionValidator(QRegularExpression("[0-9]{0,10}"), remote_code_));
  peerLabel->setBuddy(remote_code_);
  remote->addWidget(remote_code_);
  auto nameLabel = label("你的称呼（向对方显示）");
  remote->addWidget(nameLabel);
  name_ = new QLineEdit;
  name_->setObjectName("controllerName");
  name_->setMaxLength(80);
  nameLabel->setBuddy(name_);
  remote->addWidget(name_);
  connect_ = button("连接远程设备", "connectRemote", true);
  remote->addWidget(connect_);
  auto peerHint = label("对方确认后才会共享画面。");
  peerHint->setObjectName("muted");
  av::ui::set_text_role(peerHint, "caption");
  remote->addWidget(peerHint);
  remote->addStretch();
  pending_ = new av::ui::Card;
  pending_->setObjectName("approvalCard");
  auto requestLayout = new QVBoxLayout(pending_);
  requestLayout->setContentsMargins(18, 14, 18, 14);
  request_ = label("");
  request_->setObjectName("cardTitle");
  av::ui::set_text_role(request_, "heading");
  requestLayout->addWidget(request_);
  auto decisions = new QHBoxLayout;
  approve_ = button("允许本次协助", "approve", true);
  reject_ = button("拒绝", "reject");
  decisions->addWidget(approve_);
  decisions->addWidget(reject_);
  decisions->addStretch();
  requestLayout->addLayout(decisions);
  home->insertWidget(3, pending_);
  feedback_ = new av::ui::Notice({}, av::ui::Tone::Error);
  feedback_->setObjectName("feedback");
  feedback_->hide();
  home->insertWidget(4, feedback_);
  permissions_ = label("");
  permissions_->setObjectName("permissions");
  av::ui::set_text_role(permissions_, "caption");
  home->addWidget(permissions_);
  shortcut_ = label("");
  shortcut_->setObjectName("muted");
  av::ui::set_text_role(shortcut_, "caption");
  home->addWidget(shortcut_);
  auto settings = new QVBoxLayout(pages->widget(1));
  settings->setContentsMargins(28, 26, 28, 28);
  settings->setSpacing(14);
  auto settingsTitle = label("连接设置");
  settingsTitle->setObjectName("title");
  av::ui::set_text_role(settingsTitle, "title");
  settings->addWidget(settingsTitle);
  auto settingsIntro = label("业务服务地址与本机权限");
  settingsIntro->setObjectName("subtitle");
  av::ui::set_text_role(settingsIntro, "muted");
  settings->addWidget(settingsIntro);
  auto form = new QFormLayout;
  url_ = new QLineEdit;
  url_->setObjectName("serviceUrl");
  url_->setPlaceholderText("http://192.168.1.20:8080");
  form->addRow("业务服务地址", url_);
  account_ = new QLineEdit;
  account_->setPlaceholderText("生成本机连接码需要组织成员账号");
  password_ = new QLineEdit;
  password_->setEchoMode(QLineEdit::Password);
  organization_ = new QLineEdit;
  organization_->setPlaceholderText("只有一个组织时可留空");
  form->addRow("账号", account_);
  form->addRow("密码", password_);
  form->addRow("组织 ID", organization_);
  QVBoxLayout* accountLayout;
  auto accountCard = card("服务与组织账号", accountLayout);
  form->setSpacing(16);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  accountLayout->addLayout(form);
  auto accountHint = label("组织成员账号仅用于生成本机连接码；连接其他设备只需对方提供的连接码。");
  av::ui::set_text_role(accountHint, "muted");
  accountLayout->addWidget(accountHint);
  settings->addWidget(accountCard);
  settings->addWidget(label("连接期间不能修改地址。HTTPS 使用系统信任证书。"));
  auto screen = button("检查屏幕录制权限", "screenPermission"),
       input = button("检查辅助功能权限", "inputPermission");
  settings->addWidget(screen);
  settings->addWidget(input);
  connect(screen, &QPushButton::clicked, this, [] { Platform::request_screen_permission(); });
  connect(input, &QPushButton::clicked, this, [] { Platform::request_input_permission(); });
#ifndef Q_OS_MACOS
  screen->hide();
  input->hide();
#endif
  settings->addStretch();
  auto about = new QVBoxLayout(pages->widget(2));
  about->setContentsMargins(28, 26, 28, 28);
  about->setSpacing(14);
  auto aboutTitle = label("AV Remote");
  aboutTitle->setObjectName("title");
  av::ui::set_text_role(aboutTitle, "title");
  about->addWidget(aboutTitle);
  auto aboutIntro = label("面向可信局域网的远程协助");
  aboutIntro->setObjectName("subtitle");
  av::ui::set_text_role(aboutIntro, "muted");
  about->addWidget(aboutIntro);
  about->addWidget(
      label("共享主屏画面、键鼠操作与文字输入。\n\n每次协助均由本机确认，可随时通过"
            "顶部停止按钮或紧急快捷键结束。\n\n仅将连接码分享给你信任的人。"));
  about->addStretch();
  if (!test_) {
    QSettings preferences;
    restoreGeometry(preferences.value("windowGeometry").toByteArray());
    url_->setText(preferences.value("serviceUrl", "http://127.0.0.1:8080").toString());
    name_->setText(preferences.value("displayName", "协助者").toString());
    platform_ = std::make_unique<Platform>([this] { stop_all(); });
    auto hotkey = platform_->hotkey();
    shortcut_->setText(hotkey.empty() ? "全局快捷键不可用，请使用停止按钮或托盘停止。"
                                      : "紧急停止：" + QString::fromStdString(hotkey));
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
      tray_ = new QSystemTrayIcon(app_icon(), this);
      auto menu = new QMenu(this);
      av::ui::apply_theme(menu, av::ui::Theme::Light);
      menu->addAction("显示主窗口", this, [this] {
        showNormal();
        raise();
        activateWindow();
      });
      menu->addAction("立即停止协助", this, [this] { stop_all(); });
      menu->addSeparator();
      menu->addAction("退出", this, &QWidget::close);
      tray_->setContextMenu(menu);
      tray_->setToolTip("AV Remote · 准备就绪");
      tray_->show();
      connect(tray_, &QSystemTrayIcon::activated, this, [this](auto) {
        showNormal();
        raise();
      });
    }
    connect(&timer_, &QTimer::timeout, this, [this] { refresh(); });
    timer_.start(50);
  } else {
    url_->setText("http://127.0.0.1:8080");
    name_->setText("协助者");
    shortcut_->setText("紧急停止：Control + Option + F10");
  }
  auto remember = [this] {
    try {
      if (host_.snapshot().finished && controller_.snapshot().finished)
        save();
    } catch (const std::exception& e) {
      error(e.what());
    }
  };
  connect(url_, &QLineEdit::editingFinished, this, remember);
  connect(name_, &QLineEdit::editingFinished, this, remember);
  connect(copy_, &QPushButton::clicked, this, [this] {
    QApplication::clipboard()->setText(code_->text());
    status_->setText("连接码已复制，仅分享给可信任的协助者。");
  });
  connect(create_, &QPushButton::clicked, this, [this] {
    try {
      if (!controller_.snapshot().finished || regenerating_)
        return;
      auto host = host_.snapshot();
      if (!host.finished) {
        if (host.state == "waiting") {
          regenerating_ = true;
          host_.stop();
          create_->setEnabled(false);
          status_->setText("正在撤销旧连接码…");
        }
        return;
      }
      save();
      host_.start(url_->text().trimmed().toStdString(), account_->text().trimmed().toStdString(),
                  password_->text().toStdString(), organization_->text().trimmed().toStdString());
      refresh();
    } catch (const std::exception& e) {
      error(e.what());
    }
  });
  connect(connect_, &QPushButton::clicked, this, [this] {
    try {
      if (!controller_.snapshot().finished || !host_.snapshot().finished)
        return;
      if (remote_code_->text().size() != 10 || name_->text().trimmed().isEmpty())
        throw std::runtime_error("请填写 10 位连接码和称呼");
      save();
      controller_.start(url_->text().trimmed().toStdString(), remote_code_->text().toStdString(),
                        name_->text().trimmed().toUtf8().toStdString());
      if (control_)
        delete control_;
      control_ = new ControlWindow(controller_, this);
      control_->show();
      refresh();
    } catch (const std::exception& e) {
      error(e.what());
    }
  });
  connect(approve_, &QPushButton::clicked, this, [this] { host_.approve(); });
  connect(reject_, &QPushButton::clicked, this, [this] { host_.reject(); });
  connect(stop_, &QPushButton::clicked, this, [this] { stop_all(); });
  display({}, {}, test_ ? sdk::ControlPermissions{true, true, true} : sdk::control_permissions());
}

MainWindow::~MainWindow() {
  stop_all();
  if (control_)
    delete control_;
}

void MainWindow::save() {
  ui_error_.clear();
  auto address = QUrl(url_->text().trimmed());
  if ((address.scheme() != "http" && address.scheme() != "https") || address.host().isEmpty() ||
      !address.userInfo().isEmpty() || address.hasQuery() || address.hasFragment())
    throw std::runtime_error("请输入有效的 HTTP/HTTPS 服务地址");
  if (!test_) {
    QSettings p;
    p.setValue("serviceUrl", url_->text().trimmed());
    p.setValue("displayName", name_->text().trimmed());
  }
}

void MainWindow::error(const QString& s) {
  ui_error_ = s;
  status_->setText("操作未完成");
  feedback_->setText(s);
  feedback_->show();
  av::ui::set_tone(status_, av::ui::Tone::Error);
}

void MainWindow::stop_all() {
  regenerating_ = false;
  ui_error_.clear();
  host_.stop();
  controller_.stop();
  if (control_)
    control_->video()->pause();
}

void MainWindow::display(const Snapshot& h, const ControllerSnapshot& c,
                         sdk::ControlPermissions p) {
  bool idle = h.finished && c.finished;
  url_->setEnabled(idle);
  name_->setEnabled(idle);
  remote_code_->setEnabled(idle);
  create_->setEnabled(c.finished && (h.finished || h.state == "waiting") && !closing_ &&
                      !regenerating_);
  connect_->setEnabled(idle && !closing_);
  copy_->setEnabled(!h.code.empty() && (h.state == "waiting" || h.state == "pending"));
  stop_->setEnabled(!idle);
  code_->setText(copy_->isEnabled() ? QString::fromStdString(h.code) : QString());
  create_->setText(h.state == "ended" || h.state == "waiting" ? "重新生成连接码" : "生成连接码");
  auto until = QDateTime::fromString(QString::fromStdString(h.expires), Qt::ISODateWithMs);
  if (!until.isValid())
    until = QDateTime::fromString(QString::fromStdString(h.expires), Qt::ISODate);
  if (copy_->isEnabled() && until.isValid())
    expiry_->setText(QString("剩余 %1 秒 · 仅供一次申请")
                         .arg(std::max<qint64>(0, QDateTime::currentDateTimeUtc().secsTo(until))));
  else
    expiry_->setText("10 位连接码 · 5 分钟有效 · 单次使用");
  pending_->setVisible(h.state == "pending");
  request_->setText(QString::fromStdString(h.name) +
                    " 请求协助（称呼由对方填写，未经账号认证）\n允许后将共享主"
                    "屏幕，并允许键鼠操作和文字输入。");
  approve_->setEnabled(p.ready() && !closing_);
  reject_->setEnabled(!closing_);
  permissions_->setText(
      p.ready() ? "本机已具备屏幕与输入权限。"
                : QString("本机被控暂不可用：%1%2%3。请在设置中检查；仍可连接其他设备。")
                      .arg(p.screen ? "" : "屏幕录制权限不足 ", p.input ? "" : "辅助功能权限不足 ",
                           p.interactive ? "" : "当前不是可交互桌面 "));
  const auto failure = !ui_error_.isEmpty() ? ui_error_
                       : !h.error.empty()   ? QString::fromStdString(h.error)
                                            : QString::fromStdString(c.error);
  feedback_->setText(failure);
  feedback_->setVisible(!failure.isEmpty());
  status_->setText(failure.isEmpty() ? (!c.finished ? state_label(c.state) : state_label(h.state))
                                     : "连接遇到问题");
  av::ui::set_tone(status_, !failure.isEmpty() ? av::ui::Tone::Error
                            : h.state == "approved" || c.state == "connected"
                                ? av::ui::Tone::Success
                                : av::ui::Tone::Info);
  if (closing_)
    status_->setText("正在释放输入并关闭会话…");
  if (tray_) {
    bool active = h.state == "approved";
    tray_->setIcon(app_icon(active));
    tray_->setToolTip(active ? "AV Remote · 本机正在被控制 · 点击可停止"
                             : "AV Remote · " + status_->text());
  }
}

void MainWindow::refresh() {
  auto h = host_.snapshot();
  if (regenerating_ && h.finished) {
    regenerating_ = false;
    // Old authorization cleanup completes on the worker before replacing it.
    // A failed revoke must stay visible instead of silently creating a new code.
    if (h.error.empty() && !closing_) {
      try {
        host_.start(url_->text().trimmed().toStdString(), account_->text().trimmed().toStdString(),
                    password_->text().toStdString(), organization_->text().trimmed().toStdString());
        h = host_.snapshot();
      } catch (const std::exception& e) {
        error(e.what());
      }
    }
  }
  auto c = controller_.snapshot();
  if (sdk::ControlClock::now() >= permission_check_) {
    permission_cache_ = sdk::control_permissions();
    permission_check_ = sdk::ControlClock::now() + std::chrono::seconds(1);
  }
  display(h, c, permission_cache_);
  if (control_)
    control_->present(c);
  if (closing_ && h.finished && c.finished) {
    timer_.stop();
    close();
  }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  if (!sidebar_) {
    return;
  }
  const bool compact = width() < 1100;
  sidebar_->setFixedWidth(compact ? 72 : 192);
  for (auto* text : sidebar_->findChildren<QLabel*>()) {
    if (!text->objectName().isEmpty()) {
      text->setVisible(!compact);
    }
  }
  for (auto* item : sidebar_->findChildren<QPushButton*>()) {
    item->setText(compact ? QString() : item->property("navTitle").toString());
  }
}

void MainWindow::closeEvent(QCloseEvent* e) {
  stop_all();
  if (host_.snapshot().finished && controller_.snapshot().finished) {
    if (!test_)
      QSettings().setValue("windowGeometry", saveGeometry());
    if (control_)
      control_->hide();
    if (tray_)
      tray_->hide();
    e->accept();
  } else {
    closing_ = true;
    e->ignore();
  }
}
}  // namespace remote_app
