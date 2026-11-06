#include "ui.hpp"
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
  if (primary)
    b->setProperty("primary", true);
  return b;
}

QWidget* card(QString title, QVBoxLayout*& layout) {
  auto w = new QFrame;
  w->setObjectName("card");
  layout = new QVBoxLayout(w);
  layout->setContentsMargins(24, 24, 24, 24);
  layout->setSpacing(16);
  auto t = label(title);
  t->setObjectName("cardTitle");
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

// Simple monochrome line icons for the left navigation. Keeping them white
// means the same icon works for both the idle and selected navigation states.
QIcon nav_icon(char kind) {
  QPixmap pix(32, 32);
  pix.fill(Qt::transparent);
  QPainter p(&pix);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(QColor("#ffffff"), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.setBrush(Qt::NoBrush);
  if (kind == 'm') {
    p.drawRoundedRect(4, 6, 24, 16, 2, 2);
    p.drawLine(11, 27, 21, 27);
    p.drawLine(16, 22, 16, 27);
    p.drawLine(9, 14, 12, 17);
    p.drawLine(12, 17, 20, 10);
  } else if (kind == 's') {
    for (int y : {10, 16, 22}) {
      p.drawLine(5, y, 27, y);
    }
    p.setBrush(QColor("#ffffff"));
    p.drawEllipse(QPoint(11, 10), 3, 3);
    p.drawEllipse(QPoint(21, 16), 3, 3);
    p.drawEllipse(QPoint(14, 22), 3, 3);
  } else {
    p.drawEllipse(QPoint(16, 16), 11, 11);
    p.drawLine(16, 14, 16, 21);
    p.drawLine(16, 10, 16, 10);
  }
  return QIcon(pix);
}
}  // namespace

void apply_theme() {
  qApp->setStyle("Fusion");
  qApp->setWindowIcon(app_icon());
  qApp->setStyleSheet(R"(
 QWidget{font-size:14px;color:#1b2740;background:#f4f7fc;}
 QMainWindow{background:#f4f7fc;}
 QFrame#sidebar{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #0c2050,stop:0.55 #17409c,stop:1 #2f6bff);}
 QFrame#viewer{background:#0d1526;}
 QLabel{background:transparent;}
 QLabel#title{font-size:26px;font-weight:700;color:#16223a;}
 QLabel#subtitle{color:#6b7a94;}
 QLabel#cardTitle{font-size:17px;font-weight:700;color:#16223a;}
 QLabel#muted{color:#6b7a94;}
 QLabel#permissions,QLabel#textStatus{color:#4d5f7b;}
 QLabel#brandText{color:#ffffff;font-size:18px;font-weight:700;}
 QLabel#brandSub{color:#b9cff5;font-size:11px;}
 QLabel#sideCaption{color:#a8c4f5;font-size:11px;}
 QLabel#sideNote{color:#c7dbff;font-size:12px;}
 QLabel#topTitle{font-size:15px;font-weight:700;color:#16223a;}
 QLabel#connectionStatus{background:#eef3ff;border:1px solid #d8e4ff;border-radius:14px;color:#1d4ed8;padding:6px 14px;font-size:12px;font-weight:600;}
 QFrame#card{background:#ffffff;border:1px solid #e3eaf5;border-radius:14px;}
 QFrame#localCard{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #1d4ed8,stop:0.6 #2f6bff,stop:1 #5b93ff);border:0;border-radius:14px;}
 QFrame#localCard QLabel{color:#eaf2ff;}
 QFrame#localCard QLabel#cardTitle{color:#ffffff;}
 QFrame#localCard QLabel#localCodeLabel{color:#dbe8ff;font-size:12px;}
 QFrame#localCard QLabel#localHint{color:#c7dbff;font-size:12px;}
 QFrame#topbar{background:#ffffff;border-bottom:1px solid #e3eaf5;}
 QFrame#composer{background:#ffffff;border-top:1px solid #e3eaf5;}
 QLineEdit,QTextEdit{background:#f8fafd;border:1px solid #d9e2f0;border-radius:9px;padding:10px 12px;selection-background-color:#2f6bff;}
 QLineEdit:focus,QTextEdit:focus{border:2px solid #2f6bff;}
 QLineEdit:read-only{background:#f1f4fa;}
 QLineEdit#localCode{background:rgba(255,255,255,0.12);border:1px solid rgba(255,255,255,0.35);color:#ffffff;font-size:28px;font-weight:700;letter-spacing:3px;padding:10px 14px;border-radius:9px;}
 QLineEdit#localCode:read-only{background:rgba(255,255,255,0.12);}
 QPushButton{background:#ffffff;border:1px solid #d3ddec;border-radius:9px;padding:9px 16px;color:#334a6d;}
 QPushButton:hover{background:#eef4ff;border-color:#2f6bff;}
 QPushButton:pressed{background:#dbe8ff;}
 QPushButton:checked{background:#dbe8ff;border-color:#2f6bff;color:#1d4ed8;font-weight:700;}
 QPushButton[primary="true"]{background:#2f6bff;border:1px solid #2f6bff;color:#ffffff;font-weight:700;}
 QPushButton[primary="true"]:hover{background:#1d4ed8;border-color:#1d4ed8;}
 QPushButton:disabled{background:#edf1f7;color:#8a9ab2;border-color:#dce4ef;}
 QPushButton[nav="true"]{background:transparent;border:0;border-left:3px solid transparent;border-radius:8px;color:#d9e6ff;text-align:left;padding:10px 12px;font-size:14px;font-weight:500;}
 QPushButton[nav="true"]:hover{background:rgba(255,255,255,0.12);}
 QPushButton[nav="true"]:checked{background:rgba(255,255,255,0.20);border-left:3px solid #ffffff;color:#ffffff;font-weight:700;}
 QPushButton#stopSession{color:#c02b3e;background:#fff0f1;border:1px solid #f6ccd2;}
 QFrame#localCard QPushButton{background:rgba(255,255,255,0.16);border:1px solid rgba(255,255,255,0.42);color:#ffffff;}
 QFrame#localCard QPushButton:hover{background:rgba(255,255,255,0.28);}
 QFrame#localCard QPushButton[primary="true"]{background:#ffffff;border:1px solid #ffffff;color:#1d4ed8;}
 QFrame#localCard QPushButton[primary="true"]:hover{background:#eaf1ff;}
 QFrame#localCard QPushButton:disabled{background:rgba(255,255,255,0.10);color:rgba(255,255,255,0.55);border-color:rgba(255,255,255,0.2);}
 QToolBar{background:#101c33;border:0;spacing:6px;padding:8px 12px;}
 QToolBar QToolButton{color:#e8efff;background:transparent;border-radius:7px;padding:8px 12px;}
 QToolBar QToolButton:hover{background:#2f6bff;color:#ffffff;}
 QToolBar::separator{background:#2a3b59;width:1px;margin:6px 4px;}
 QScrollArea{border:0;background:#0d1526;}
 QStatusBar{background:#ffffff;}
 )");
}

ControlWindow::ControlWindow(Controller& controller, QWidget* parent)
    : QMainWindow(parent), controller_(controller) {
  setWindowFlag(Qt::Window);
  setWindowTitle("远程桌面 — AV Remote");
  resize(1180, 780);
  setMinimumSize(720, 520);
  auto toolbar = addToolBar("会话操作");
  toolbar->setMovable(false);
  auto fit = toolbar->addAction("适应窗口");
  auto original = toolbar->addAction("原始比例");
  auto full = toolbar->addAction("全屏");
  auto pause = toolbar->addAction("暂停输入");
  toolbar->addSeparator();
  auto disconnect = toolbar->addAction("断开连接");
  auto center = new QFrame;
  center->setObjectName("viewer");
  center->setFrameShape(QFrame::NoFrame);
  auto layout = new QVBoxLayout(center);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  scroll_ = new QScrollArea;
  scroll_->setWidgetResizable(true);
  video_ = new VideoWidget;
  scroll_->setWidget(video_);
  layout->addWidget(scroll_, 1);
  auto composer = new QFrame;
  composer->setObjectName("composer");
  auto composerLayout = new QVBoxLayout(composer);
  composerLayout->setContentsMargins(16, 12, 16, 14);
  composerLayout->setSpacing(10);
  status_ = label("点击远程画面选择目标，再发送文字。离开窗口会暂停输入。");
  status_->setObjectName("textStatus");
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
  topbar->setFixedHeight(60);
  auto top = new QHBoxLayout(topbar);
  top->setContentsMargins(24, 0, 24, 0);
  auto topTitle = label("远程控制");
  topTitle->setObjectName("topTitle");
  top->addWidget(topTitle);
  top->addStretch();
  status_ = label("准备就绪");
  status_->setObjectName("connectionStatus");
  top->addWidget(status_);
  outer->addWidget(topbar);
  auto horizontal = new QHBoxLayout;
  horizontal->setContentsMargins(0, 0, 0, 0);
  horizontal->setSpacing(0);
  outer->addLayout(horizontal, 1);
  auto side = new QFrame;
  side->setObjectName("sidebar");
  side->setFrameShape(QFrame::NoFrame);
  side->setFixedWidth(216);
  auto nav = new QVBoxLayout(side);
  nav->setContentsMargins(16, 24, 16, 20);
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
  auto brandSub = label("安全连接 · 需对方确认");
  brandSub->setObjectName("brandSub");
  brandText->addWidget(brandName);
  brandText->addWidget(brandSub);
  brandRow->addLayout(brandText);
  nav->addLayout(brandRow);
  nav->addSpacing(28);
  auto caption = label("工作台");
  caption->setObjectName("sideCaption");
  nav->addWidget(caption);
  auto pages = new QStackedWidget;
  auto navigation = new QButtonGroup(this);
  navigation->setExclusive(true);
  const char icons[] = {'m', 's', 'i'};
  for (auto title : {"远程控制", "设置", "关于"}) {
    int index = pages->count();
    auto b = button(title, QString("nav") + title);
    b->setProperty("nav", true);
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
  nav->addWidget(note);
  horizontal->addWidget(side);
  horizontal->addWidget(pages, 1);
  setCentralWidget(shell);
  auto home = new QVBoxLayout(pages->widget(0));
  home->setContentsMargins(28, 26, 28, 24);
  home->setSpacing(14);
  auto title = label("远程协助");
  title->setObjectName("title");
  home->addWidget(title);
  auto intro = label("输入对方连接码发起协助，或生成本机连接码接受协助。");
  intro->setObjectName("subtitle");
  home->addWidget(intro);
  home->addSpacing(4);
  auto cards = new QHBoxLayout;
  cards->setSpacing(18);
  QVBoxLayout *local, *remote;
  auto localCard = card("允许控制本机", local);
  localCard->setObjectName("localCard");
  auto remoteCard = card("连接远程设备", remote);
  cards->addWidget(localCard, 1);
  cards->addWidget(remoteCard, 1);
  home->addLayout(cards);
  auto codeLabel = label("本机连接码");
  codeLabel->setObjectName("localCodeLabel");
  local->addWidget(codeLabel);
  code_ = new QLineEdit;
  code_->setReadOnly(true);
  code_->setObjectName("localCode");
  code_->setPlaceholderText("尚未生成");
  codeLabel->setBuddy(code_);
  local->addWidget(code_);
  expiry_ = label("10 位连接码 · 5 分钟有效 · 单次使用");
  expiry_->setObjectName("localHint");
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
  remote->addWidget(peerHint);
  remote->addStretch();
  pending_ = new QFrame;
  pending_->setObjectName("card");
  auto requestLayout = new QVBoxLayout(pending_);
  requestLayout->setContentsMargins(18, 14, 18, 14);
  request_ = label("");
  request_->setObjectName("cardTitle");
  requestLayout->addWidget(request_);
  auto decisions = new QHBoxLayout;
  approve_ = button("允许本次协助", "approve", true);
  reject_ = button("拒绝", "reject");
  decisions->addWidget(approve_);
  decisions->addWidget(reject_);
  decisions->addStretch();
  requestLayout->addLayout(decisions);
  home->addWidget(pending_);
  permissions_ = label("");
  permissions_->setObjectName("permissions");
  home->addWidget(permissions_);
  shortcut_ = label("");
  shortcut_->setObjectName("muted");
  home->addWidget(shortcut_);
  stop_ = button("停止当前会话", "stopSession");
  home->addWidget(stop_);
  home->addStretch();
  auto settings = new QVBoxLayout(pages->widget(1));
  settings->setContentsMargins(28, 26, 28, 28);
  settings->setSpacing(14);
  auto settingsTitle = label("连接设置");
  settingsTitle->setObjectName("title");
  settings->addWidget(settingsTitle);
  auto settingsIntro = label("业务服务地址与本机权限");
  settingsIntro->setObjectName("subtitle");
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
  settings->addLayout(form);
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
  about->addWidget(aboutTitle);
  auto aboutIntro = label("面向可信局域网的远程协助");
  aboutIntro->setObjectName("subtitle");
  about->addWidget(aboutIntro);
  about->addWidget(
      label("Qt6 "
            "双平台远程协助\n\n主屏画面、键鼠操作与显式文字发送。\n每次协助需要"
            "本地确认，不支持无人值守。\n\n使用 Qt、LiveKit SDK 和 WebRTC。"));
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
  status_->setText(s);
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
  if (!ui_error_.isEmpty())
    status_->setText(ui_error_);
  else if (!h.error.empty())
    status_->setText(QString::fromStdString(h.error));
  else if (!c.error.empty())
    status_->setText(QString::fromStdString(c.error));
  else
    status_->setText(!c.finished ? state_label(c.state) : state_label(h.state));
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
