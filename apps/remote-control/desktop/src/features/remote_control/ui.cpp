#include "ui.hpp"
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QStackedWidget>
#include <QStyle>
#include <QToolBar>
#include <QUrl>

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
}  // namespace

void apply_theme() {
  qApp->setStyle("Fusion");
  qApp->setWindowIcon(app_icon());
  qApp->setStyleSheet(R"(
 QWidget{font-size:14px;color:#172033;} QMainWindow{background:#f3f6fb;}
 QLabel{background:transparent;} QLabel#title{font-size:27px;font-weight:650;} QLabel#cardTitle{font-size:19px;font-weight:600;} QLabel#muted{color:#526174;}
 QFrame#card{background:white;border:1px solid #dce3ef;border-radius:12px;}
 QPushButton{background:white;border:1px solid #b9c5d6;border-radius:7px;padding:7px 16px;}
 QPushButton:hover{background:#eef4ff;border-color:#2563eb;} QPushButton:pressed{background:#dbeafe;} QPushButton:checked{background:#dbeafe;border-color:#2563eb;color:#1d4ed8;font-weight:600;}
 QPushButton[primary="true"]{background:#2563eb;border-color:#2563eb;color:white;font-weight:600;} QPushButton[primary="true"]:hover{background:#1d4ed8;}
 QPushButton:disabled{background:#edf1f7;color:#68778b;border-color:#d3dce8;} QPushButton:focus,QLineEdit:focus,QTextEdit:focus{border:2px solid #2563eb;}
 QLineEdit,QTextEdit{background:white;border:1px solid #aab8cc;border-radius:6px;padding:9px;selection-background-color:#2563eb;}
 QLineEdit:read-only{background:#f3f6fb;} QLineEdit#localCode{font-size:29px;font-weight:600;letter-spacing:2px;}
 QToolBar{background:white;border-bottom:1px solid #dce3ef;spacing:10px;padding:6px;}
 QToolButton{padding:6px;} QToolButton:hover{background:#e8f0ff;} QScrollArea{border:0;} QStatusBar{background:white;}
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
  auto center = new QWidget;
  auto layout = new QVBoxLayout(center);
  layout->setContentsMargins(12, 12, 12, 12);
  scroll_ = new QScrollArea;
  scroll_->setWidgetResizable(true);
  video_ = new VideoWidget;
  scroll_->setWidget(video_);
  layout->addWidget(scroll_, 1);
  status_ = label("点击远程画面选择目标，再发送文字。离开窗口会暂停输入。");
  status_->setObjectName("textStatus");
  layout->addWidget(status_);
  auto row = new QHBoxLayout;
  text_ = new QTextEdit;
  text_->setAccessibleName("发送到远端的文字");
  text_->setPlaceholderText("输入文字后点击发送，最多 256 个字符。Enter 为换行。");
  text_->setMaximumHeight(86);
  text_->setObjectName("remoteText");
  send_ = button("发送文字", "sendText", true);
  row->addWidget(text_, 1);
  row->addWidget(send_);
  layout->addLayout(row);
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
  resize(1080, 740);
  setMinimumSize(920, 680);
  setWindowIcon(app_icon());
  auto shell = new QWidget;
  auto horizontal = new QHBoxLayout(shell);
  horizontal->setContentsMargins(0, 0, 0, 0);
  horizontal->setSpacing(0);
  auto side = new QWidget;
  side->setFixedWidth(176);
  side->setStyleSheet("background:#eaf0f9;");
  auto nav = new QVBoxLayout(side);
  nav->setContentsMargins(16, 28, 16, 20);
  nav->setSpacing(14);
  auto brand = label("AV Remote");
  brand->setStyleSheet("font-size:21px;font-weight:700;color:#1d4ed8;");
  nav->addWidget(brand);
  nav->addSpacing(32);
  auto pages = new QStackedWidget;
  auto navigation = new QButtonGroup(this);
  navigation->setExclusive(true);
  for (auto title : {"远程控制", "设置", "关于"}) {
    auto b = button(title, QString("nav") + title);
    int index = pages->count();
    b->setCheckable(true);
    navigation->addButton(b, index);
    b->setChecked(index == 0);
    auto page = new QWidget;
    pages->addWidget(page);
    connect(b, &QPushButton::clicked, this, [pages, index] { pages->setCurrentIndex(index); });
    nav->addWidget(b);
  }
  nav->addStretch();
  nav->addWidget(label("局域网协助\n每次连接均需确认"));
  horizontal->addWidget(side);
  horizontal->addWidget(pages, 1);
  setCentralWidget(shell);
  auto home = new QVBoxLayout(pages->widget(0));
  home->setContentsMargins(28, 28, 28, 24);
  home->setSpacing(16);
  auto title = label("连接彼此，协助就在眼前");
  title->setObjectName("title");
  home->addWidget(title);
  auto intro = label("共享连接码，确认后即可开始。你始终可以停止共享。");
  intro->setObjectName("muted");
  home->addWidget(intro);
  auto cards = new QHBoxLayout;
  QVBoxLayout *local, *remote;
  auto localCard = card("允许控制本机", local);
  auto remoteCard = card("连接远程设备", remote);
  cards->addWidget(localCard, 1);
  cards->addWidget(remoteCard, 1);
  home->addLayout(cards);
  auto codeLabel = label("本机连接码");
  local->addWidget(codeLabel);
  code_ = new QLineEdit;
  code_->setReadOnly(true);
  code_->setObjectName("localCode");
  code_->setPlaceholderText("尚未生成");
  codeLabel->setBuddy(code_);
  local->addWidget(code_);
  expiry_ = label("10 位连接码 · 5 分钟有效 · 单次使用");
  expiry_->setObjectName("muted");
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
  remote->addWidget(label("对方确认后才会共享画面。"));
  remote->addStretch();
  pending_ = new QFrame;
  pending_->setObjectName("card");
  auto requestLayout = new QVBoxLayout(pending_);
  requestLayout->setContentsMargins(18, 14, 18, 14);
  request_ = label("");
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
  status_ = label("准备就绪");
  status_->setObjectName("connectionStatus");
  home->addWidget(status_);
  shortcut_ = label("");
  shortcut_->setObjectName("muted");
  home->addWidget(shortcut_);
  stop_ = button("停止当前会话", "stopSession");
  stop_->setStyleSheet("color:#b91c1c;");
  home->addWidget(stop_);
  home->addStretch();
  auto settings = new QVBoxLayout(pages->widget(1));
  settings->setContentsMargins(28, 28, 28, 28);
  auto settingsTitle = label("连接设置");
  settingsTitle->setObjectName("title");
  settings->addWidget(settingsTitle);
  auto form = new QFormLayout;
  url_ = new QLineEdit;
  url_->setObjectName("serviceUrl");
  url_->setPlaceholderText("http://192.168.1.20:8080");
  form->addRow("业务服务地址", url_);
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
  about->setContentsMargins(28, 28, 28, 28);
  auto aboutTitle = label("AV Remote");
  aboutTitle->setObjectName("title");
  about->addWidget(aboutTitle);
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
      host_.start(url_->text().trimmed().toStdString());
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
        host_.start(url_->text().trimmed().toStdString());
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
