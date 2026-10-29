#include "window.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <algorithm>

namespace meeting {
namespace {
QPushButton* button(QString text, QString name, QWidget* parent = nullptr) {
  auto* b = new QPushButton(text, parent);
  b->setObjectName(name);
  b->setMinimumHeight(38);
  b->setCursor(Qt::PointingHandCursor);
  return b;
}

QIcon actionIcon(int kind) {
  QPixmap pix(48, 48);
  pix.fill(Qt::transparent);
  QPainter p(&pix);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(QColor("#1768da"), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  if (kind == 0) {
    p.drawRoundedRect(QRectF(7, 7, 34, 34), 8, 8);
    p.drawLine(24, 15, 24, 33);
    p.drawLine(15, 24, 33, 24);
  } else if (kind == 1) {
    p.drawRoundedRect(QRectF(5, 12, 27, 25), 5, 5);
    QPolygonF shape;
    shape << QPointF(33, 19) << QPointF(43, 13) << QPointF(43, 36) << QPointF(33, 30);
    p.drawPolyline(shape);
  } else {
    p.drawRoundedRect(QRectF(7, 10, 34, 31), 5, 5);
    p.drawLine(15, 6, 15, 15);
    p.drawLine(33, 6, 33, 15);
    p.drawLine(8, 21, 40, 21);
    p.drawPoint(17, 29);
    p.drawPoint(25, 29);
    p.drawPoint(33, 29);
  }
  return QIcon(pix);
}

QLabel* label(QString text, QString name = {}) {
  auto* l = new QLabel(text);
  l->setObjectName(name);
  l->setWordWrap(true);
  return l;
}

QString str(const std::string& s) {
  return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

QString stateName(QString s) {
  return QMap<QString, QString>{{"active", "进行中"},
                                {"scheduled", "待开始"},
                                {"ending", "正在结束"},
                                {"ended", "已结束"},
                                {"cancelled", "已取消"}}
      .value(s, s);
}

QString timeText(QString iso) {
  return QDateTime::fromString(iso, Qt::ISODate).toLocalTime().toString("MM月dd日 hh:mm");
}

QString numberFrom(QString text) {
  auto m =
      QRegularExpression("(?<![0-9])([0-9]{3})[ -]?([0-9]{3})[ -]?([0-9]{3})(?![0-9])").match(text);
  return m.hasMatch() ? m.captured(1) + m.captured(2) + m.captured(3) : QString();
}
}  // namespace

Window::Window(bool running) : running_(running) {
  setObjectName("conferenceWindow");
  setWindowTitle("AV 会议");
  resize(1180, 780);
  setMinimumSize(800, 560);
  QSettings prefs;
  api_.base = prefs.value("server", "http://127.0.0.1:8080").toString();
  cameraIndex_ = prefs.value("camera", 0).toInt();
  if (running_)
    restoreGeometry(prefs.value("geometry").toByteArray());
  setStyleSheet(
      R"(QWidget {font-size:14px;color:#253044;} QMainWindow,QDialog {background:#f5f7fb;} QLabel#brand {font-size:25px;font-weight:600;color:#1768da;} QLabel#heading {font-size:27px;font-weight:600;} QLabel#subtle {color:#64738b;} QLabel#notice {color:#a13b21;background:#fff2e7;border-radius:5px;padding:7px;} QPushButton {background:white;border:1px solid #d9e0ea;border-radius:7px;padding:8px 15px;} QPushButton:hover {background:#eaf2ff;border-color:#87afea;} QPushButton:pressed {background:#d9e8ff;} QPushButton:focus,QLineEdit:focus,QPlainTextEdit:focus,QComboBox:focus {border:2px solid #1971e6;} QPushButton:disabled {color:#8993a3;background:#edf0f5;} QPushButton#primary {background:#1768da;color:white;border:1px solid #1768da;} QPushButton#danger {color:#c0313e;border-color:#e9c6cb;} QLineEdit,QPlainTextEdit,QComboBox,QSpinBox,QDateTimeEdit {background:white;border:1px solid #d6deea;border-radius:5px;padding:7px;selection-background-color:#1768da;} QListWidget {background:white;border:1px solid #e1e6ee;border-radius:9px;padding:8px;outline:0;} QListWidget::item {padding:14px;border-bottom:1px solid #eef1f6;} QListWidget::item:selected {background:#e6efff;color:#174b96;} QTabWidget::pane {border:1px solid #d9e0ea;background:white;} QTabBar::tab {padding:10px 20px;background:#edf2f9;} QTabBar::tab:selected {background:white;color:#1768da;} QCheckBox {spacing:8px;} QWidget#meetingPage {background:#151a22;} QWidget#meetingPage QLabel {color:#dfe7f5;} QWidget#meetingPage QPushButton {background:#252e3d;color:#e7edf7;border-color:#3c475a;} QWidget#meetingPage QPushButton#danger {color:#ff929c;} QWidget#meetingPage QPlainTextEdit,QWidget#meetingPage QListWidget {background:#202834;color:#e5ecf7;border-color:#354052;} QWidget#meetingPage QListWidget::item {border-bottom:1px solid #354052;} QWidget#meetingPage QTabWidget::pane {background:#202834;border-color:#354052;})");
  auto* root = new QWidget;
  auto* outer = new QVBoxLayout(root);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);
  notice_ = label({}, "notice");
  notice_->hide();
  outer->addWidget(notice_);
  pages_ = new QStackedWidget;
  outer->addWidget(pages_, 1);
  setCentralWidget(root);
  home_ = new QWidget;
  home_->setObjectName("homePage");
  auto* h = new QVBoxLayout(home_);
  h->setContentsMargins(38, 26, 38, 28);
  h->setSpacing(20);
  auto* bar = new QHBoxLayout;
  bar->addWidget(label("AV 会议", "brand"));
  bar->addStretch();
  account_ = label("游客", "subtle");
  bar->addWidget(account_);
  auto* auth = button("登录 / 注册", "accountButton");
  connect(auth, &QPushButton::clicked, this, &Window::login);
  bar->addWidget(auth);
  auto* setting = button("设置", "settingsButton");
  connect(setting, &QPushButton::clicked, this, &Window::settings);
  bar->addWidget(setting);
  h->addLayout(bar);
  h->addSpacing(14);
  h->addWidget(label("每一次沟通，都更近一步", "heading"));
  h->addWidget(label("开启一场会议，与团队保持连接。", "subtle"));
  auto* actions = new QHBoxLayout;
  actions->setSpacing(20);
  auto* add = button("加入会议", "joinButton");
  auto* quick = button("快速会议", "quickButton");
  auto* plan = button("预定会议", "scheduleButton");
  int actionIndex = 0;
  for (auto* b : {add, quick, plan}) {
    b->setIcon(actionIcon(actionIndex++));
    b->setIconSize(QSize(48, 48));
    b->setMinimumHeight(118);
    actions->addWidget(b);
  }
  h->addLayout(actions);
  connect(add, &QPushButton::clicked, this, &Window::joinDialog);
  connect(plan, &QPushButton::clicked, this, [this] { schedule(); });
  connect(quick, &QPushButton::clicked, this, [this] {
    if (api_.token.isEmpty()) {
      login();
      return;
    }
    api_.request("/api/v1/conference/meetings",
                 {{"title", user_.value("name").toString() + "的会议"}, {"duration_minutes", 60}},
                 [this](QJsonValue v, QString e) {
                   if (!e.isEmpty()) {
                     message(e);
                     return;
                   }
                   join(v.toObject().value("number").toString(), user_.value("name").toString(),
                        {});
                 });
  });
  auto* lists = new QHBoxLayout;
  auto* left = new QVBoxLayout;
  left->addWidget(label("我的会议", "sectionTitle"));
  upcoming_ = new QListWidget;
  upcoming_->setObjectName("upcomingMeetings");
  left->addWidget(upcoming_);
  auto* right = new QVBoxLayout;
  right->addWidget(label("历史会议", "sectionTitle"));
  history_ = new QListWidget;
  history_->setObjectName("meetingHistory");
  upcoming_->addItem("登录后创建会议，或直接加入会议");
  history_->addItem("暂无历史会议");
  right->addWidget(history_);
  lists->addLayout(left, 3);
  lists->addLayout(right, 2);
  h->addLayout(lists, 1);
  h->addWidget(label("默认不开启摄像头和麦克风 · 最多 25 人", "subtle"));
  connect(upcoming_, &QListWidget::itemDoubleClicked, this, [this] { showDetails(upcoming_); });
  connect(history_, &QListWidget::itemDoubleClicked, this, [this] { showDetails(history_); });
  pages_->addWidget(home_);
  waiting_ = new QWidget;
  waiting_->setObjectName("waitingPage");
  auto* w = new QVBoxLayout(waiting_);
  w->setContentsMargins(100, 50, 100, 50);
  w->addStretch();
  w->addWidget(label("会议即将开始", "heading"));
  waitingText_ = label("正在提交入会申请…", "waitingText");
  w->addWidget(waitingText_);
  w->addSpacing(20);
  auto* cancel = button("取消入会", "cancelJoin");
  connect(cancel, &QPushButton::clicked, this, [this] { leave(); });
  w->addWidget(cancel);
  w->addStretch();
  pages_->addWidget(waiting_);
  meeting_ = new QWidget;
  meeting_->setObjectName("meetingPage");
  auto* meet = new QVBoxLayout(meeting_);
  meet->setContentsMargins(16, 12, 16, 12);
  auto* titlebar = new QHBoxLayout;
  meetingTitle_ = label("AV 会议", "meetingTitle");
  titlebar->addWidget(meetingTitle_, 1);
  connection_ = label("正在连接", "connectionState");
  titlebar->addWidget(connection_);
  layout_ = new QComboBox;
  layout_->setObjectName("layoutMode");
  layout_->addItems({"宫格布局", "演讲者布局", "共享优先"});
  connect(layout_, &QComboBox::currentIndexChanged, this, [this] {
    videoPage_ = 0;
    updateVideos();
  });
  titlebar->addWidget(layout_);
  meet->addLayout(titlebar);
  auto* content = new QHBoxLayout;
  auto* canvas = new QWidget;
  grid_ = new QGridLayout(canvas);
  grid_->setContentsMargins(0, 0, 0, 0);
  grid_->setSpacing(8);
  content->addWidget(canvas, 1);
  for (int i = 0; i < 9; i++) {
    auto* tile = new VideoWidget(canvas);
    videos_.push_back(tile);
    tile->hide();
  }
  side_ = new QTabWidget;
  side_->setObjectName("meetingSidePanel");
  side_->setMinimumWidth(240);
  side_->setMaximumWidth(310);
  members_ = new QListWidget;
  members_->setObjectName("memberList");
  side_->addTab(members_, "成员");
  auto* chat = new QWidget;
  auto* chatLayout = new QVBoxLayout(chat);
  chatLog_ = new QPlainTextEdit;
  chatLog_->setReadOnly(true);
  chatLog_->setObjectName("chatHistory");
  chatInput_ = new QPlainTextEdit;
  chatInput_->setObjectName("chatInput");
  chatInput_->setPlaceholderText("发送给所有人，Ctrl+Enter 发送");
  chatInput_->setMaximumHeight(100);
  auto* send = button("发送", "sendChat");
  chatLayout->addWidget(chatLog_, 1);
  chatLayout->addWidget(chatInput_);
  chatLayout->addWidget(send);
  side_->addTab(chat, "聊天");
  auto sendMessage = [this] {
    try {
      if (session_.send_chat(chatInput_->toPlainText().toUtf8().toStdString()))
        chatInput_->clear();
    } catch (const std::exception& e) {
      message(str(e.what()));
    }
  };
  connect(send, &QPushButton::clicked, this, sendMessage);
  connect(new QShortcut(QKeySequence("Ctrl+Return"), chatInput_), &QShortcut::activated, this,
          sendMessage);
  content->addWidget(side_);
  meet->addLayout(content, 1);
  auto* pagination = new QHBoxLayout;
  pagination->addStretch();
  auto* prev = button("上一页", "previousPage");
  auto* next = button("下一页", "nextPage");
  pageLabel_ = label("1 / 1");
  pagination->addWidget(prev);
  pagination->addWidget(pageLabel_);
  pagination->addWidget(next);
  pagination->addStretch();
  meet->addLayout(pagination);
  connect(prev, &QPushButton::clicked, this, [this] {
    videoPage_ = std::max(0, videoPage_ - 1);
    updateVideos();
  });
  connect(next, &QPushButton::clicked, this, [this] {
    ++videoPage_;
    updateVideos();
  });
  auto* toolbar = new QGridLayout;
  toolbar_ = toolbar;
  mic_ = button("开启麦克风", "microphoneButton");
  camera_ = button("开启摄像头", "cameraButton");
  share_ = button("共享屏幕", "shareButton");
  auto* inviteButton = button("邀请", "inviteButton");
  manage_ = button("管理成员", "manageButton");
  auto* chatButton = button("聊天", "chatButton");
  auto* leaveButton = button("离开会议", "danger");
  int col = 0;
  for (auto* b : {mic_, camera_, share_, inviteButton, manage_, chatButton, leaveButton}) {
    toolbarButtons_.push_back(b);
    toolbar->addWidget(b, col / 7, col % 7);
    ++col;
  }
  meet->addLayout(toolbar);
  connect(mic_, &QPushButton::clicked, this, &Window::toggleMicrophone);
  connect(camera_, &QPushButton::clicked, this, &Window::toggleCamera);
  connect(share_, &QPushButton::clicked, this, &Window::toggleShare);
  connect(inviteButton, &QPushButton::clicked, this, &Window::invite);
  connect(manage_, &QPushButton::clicked, this, &Window::memberMenu);
  connect(members_, &QListWidget::itemDoubleClicked, this, [this] { memberMenu(); });
  connect(chatButton, &QPushButton::clicked, this, [this] {
    side_->setVisible(!side_->isVisible() || side_->currentIndex() != 1);
    side_->setCurrentIndex(1);
  });
  connect(leaveButton, &QPushButton::clicked, this, [this] { leave(); });
  pages_->addWidget(meeting_);
  connect(&tick_, &QTimer::timeout, this, [this] {
    auto snap = session_.snapshot();
    applyMedia(snap);
    if ((closing_ || leaving_) && snap.finished) {
      if (closing_) {
        close();
      } else
        exitMeeting();
    }
  });
  connect(&poll_, &QTimer::timeout, this, &Window::poll);
  connect(&homeTimer_, &QTimer::timeout, this, &Window::refreshHome);
  if (running_) {
    tick_.start(50);
    poll_.start(2000);
    homeTimer_.start(30000);
  }
  showHome();
}

void Window::message(QString text) {
  notice_->setText(text);
  notice_->setVisible(!text.isEmpty());
}

QString Window::path(QString suffix) const {
  return "/api/v1/conference/meetings/" + meetingId_ +
         (suffix.isEmpty() ? QString() : "/" + suffix);
}

bool Window::isHost() const {
  return !business_.value("self").toString().isEmpty() &&
         business_.value("self") == business_.value("host");
}

void Window::showHome() {
  pages_->setCurrentWidget(home_);
  if (running_)
    refreshHome();
}

void Window::login() {
  if (!api_.token.isEmpty()) {
    if (QMessageBox::question(this, "退出登录", "退出当前账号？") == QMessageBox::Yes) {
      api_.request("/api/v1/auth/logout", {}, [this](QJsonValue, QString e) {
        if (!e.isEmpty()) {
          message(e);
          return;
        }
        api_.cancel();
        api_.token.clear();
        user_ = {};
        account_->setText("游客");
        refreshHome();
      });
    }
    return;
  }
  auto* d = new QDialog(this);
  d->setAttribute(Qt::WA_DeleteOnClose);
  d->setWindowTitle("登录 AV 会议");
  auto* form = new QFormLayout(d);
  auto* name = new QLineEdit;
  name->setObjectName("loginName");
  auto* password = new QLineEdit;
  password->setObjectName("loginPassword");
  password->setEchoMode(QLineEdit::Password);
  auto* status = label("用户名 3–32 位，密码至少 10 字节");
  auto* submit = button("登录", "primary");
  auto* reg = button("注册并登录", "registerButton");
  form->addRow("用户名", name);
  form->addRow("密码", password);
  form->addRow(status);
  form->addRow(submit, reg);
  auto run = [this, d, name, password, status, submit, reg](QString action) {
    submit->setEnabled(false);
    reg->setEnabled(false);
    QPointer<QDialog> guard = d;
    api_.request("/api/v1/auth/" + action, {{"name", name->text()}, {"password", password->text()}},
                 [this, guard, status, submit, reg](QJsonValue v, QString e) {
                   if (!guard)
                     return;
                   submit->setEnabled(true);
                   reg->setEnabled(true);
                   if (!e.isEmpty()) {
                     status->setText(e);
                     return;
                   }
                   api_.token = v.toObject().value("session_token").toString();
                   api_.guest.clear();
                   user_ = v.toObject().value("user").toObject();
                   account_->setText(user_.value("name").toString());
                   guard->accept();
                   message({});
                   refreshHome();
                 });
  };
  connect(submit, &QPushButton::clicked, d, [run] { run("login"); });
  connect(reg, &QPushButton::clicked, d, [run] { run("register"); });
  connect(password, &QLineEdit::returnPressed, submit, &QPushButton::click);
  d->open();
}

void Window::refreshDevices() {
  try {
    cameras_ = demo::sdk::video_capture_devices();
    screens_ = demo::sdk::screen_capture_devices();
    if (cameraIndex_ >= int(cameras_.size()))
      cameraIndex_ = 0;
  } catch (const std::exception& e) {
    message(str(e.what()));
  }
}

void Window::settings() {
  auto* d = new QDialog(this);
  d->setWindowTitle("设置");
  d->setAttribute(Qt::WA_DeleteOnClose);
  auto* form = new QFormLayout(d);
  auto* server = new QLineEdit(api_.base);
  server->setObjectName("serverAddress");
  server->setEnabled(meetingId_.isEmpty());
  form->addRow("会议服务地址", server);
  auto* devices = new QComboBox;
  for (auto& c : cameras_)
    devices->addItem(str(c.name));
  devices->setCurrentIndex(cameraIndex_);
  form->addRow("摄像头", devices);
  auto* refresh = button("刷新设备", "refreshDevices");
  form->addRow(refresh);
  connect(refresh, &QPushButton::clicked, d, [this, devices] {
    refreshDevices();
    devices->clear();
    for (auto& c : cameras_)
      devices->addItem(str(c.name));
  });
  form->addRow(label("麦克风与扬声器使用系统默认设备。"));
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::rejected, d, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, d, [this, d, server, devices] {
    QUrl url(server->text().trimmed());
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https") ||
        url.host().isEmpty() || !url.userInfo().isEmpty()) {
      QMessageBox::warning(d, "地址无效", "请输入有效的 HTTP/HTTPS 服务地址");
      return;
    }
    auto base = server->text().trimmed();
    while (base.endsWith('/'))
      base.chop(1);
    if (api_.base != base) {
      api_.cancel();
      api_.base = base;
      api_.token.clear();
      api_.guest.clear();
      user_ = {};
      account_->setText("游客");
    }
    cameraIndex_ = std::max(0, devices->currentIndex());
    QSettings p;
    p.setValue("server", api_.base);
    p.setValue("camera", cameraIndex_);
    d->accept();
    refreshHome();
  });
  d->open();
}

void Window::refreshHome() {
  if (!meetingId_.isEmpty())
    return;
  if (api_.token.isEmpty()) {
    upcoming_->clear();
    history_->clear();
    upcoming_->addItem("登录后创建会议，或直接加入会议");
    return;
  }
  api_.request(
      "/api/v1/conference/meetings", {},
      [this](QJsonValue v, QString e) {
        if (!e.isEmpty()) {
          message(e);
          return;
        }
        meetings_ = v.toArray();
        upcoming_->clear();
        history_->clear();
        for (auto value : meetings_) {
          auto m = value.toObject();
          QString state = m.value("state").toString();
          auto* list = (state == "ended" || state == "cancelled") ? history_ : upcoming_;
          auto* item = new QListWidgetItem(
              m.value("title").toString() + "\n" + timeText(m.value("starts_at").toString()) +
                  "  ·  " + stateName(state) + "\n会议号 " + m.value("number").toString(),
              list);
          item->setData(Qt::UserRole, m);
        }
        if (upcoming_->count() == 0)
          upcoming_->addItem("暂无会议，开始一次新的交流吧");
      },
      true);
}

void Window::schedule(QJsonObject existing) {
  if (api_.token.isEmpty()) {
    login();
    return;
  }
  auto* d = new QDialog(this);
  d->setAttribute(Qt::WA_DeleteOnClose);
  d->setWindowTitle(existing.isEmpty() ? "预定会议" : "编辑预约");
  auto* form = new QFormLayout(d);
  auto* title = new QLineEdit(existing.value("title").toString());
  title->setObjectName("scheduleTitle");
  title->setMaxLength(100);
  auto* time = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(3600));
  time->setCalendarPopup(true);
  if (existing.contains("starts_at"))
    time->setDateTime(
        QDateTime::fromString(existing.value("starts_at").toString(), Qt::ISODate).toLocalTime());
  auto* duration = new QSpinBox;
  duration->setRange(1, 1440);
  duration->setValue(existing.value("duration_minutes").toInt(60));
  duration->setSuffix(" 分钟");
  auto* password = new QLineEdit;
  password->setEchoMode(QLineEdit::Password);
  password->setPlaceholderText(existing.isEmpty() ? "可选" : "留空保留现有密码");
  auto* waiting = new QCheckBox("启用等候室");
  waiting->setChecked(existing.value("waiting_room").toBool(true));
  auto* status = label({});
  auto* save = button("保存预约", "primary");
  form->addRow("会议主题", title);
  form->addRow("开始时间", time);
  form->addRow("预计时长", duration);
  form->addRow("会议密码", password);
  form->addRow(waiting);
  form->addRow(status);
  form->addRow(save);
  connect(save, &QPushButton::clicked, d,
          [this, d, title, time, duration, password, waiting, status, save, existing] {
            if (title->text().trimmed().isEmpty()) {
              status->setText("请填写会议主题");
              return;
            }
            save->setEnabled(false);
            QJsonObject data{{"title", title->text().trimmed()},
                             {"starts_at", time->dateTime().toUTC().toString(Qt::ISODate)},
                             {"duration_minutes", duration->value()},
                             {"password", password->text()},
                             {"waiting_room", waiting->isChecked()},
                             {"scheduled", true}};
            auto url = existing.isEmpty() ? QString("/api/v1/conference/meetings")
                                          : QString("/api/v1/conference/meetings/") +
                                                existing.value("id").toString() + "/edit";
            QPointer<QDialog> guard = d;
            api_.request(url, data, [this, guard, status, save](QJsonValue, QString e) {
              if (!guard)
                return;
              save->setEnabled(true);
              if (!e.isEmpty()) {
                status->setText(e);
                return;
              }
              guard->accept();
              refreshHome();
            });
          });
  d->open();
}

void Window::joinDialog() {
  auto* d = new QDialog(this);
  d->setAttribute(Qt::WA_DeleteOnClose);
  d->setWindowTitle("加入会议");
  auto* form = new QFormLayout(d);
  auto* number = new QLineEdit;
  number->setObjectName("meetingNumber");
  number->setPlaceholderText("会议号或邀请文本");
  auto* name = new QLineEdit(user_.value("name").toString());
  name->setObjectName("displayName");
  name->setMaxLength(40);
  auto* password = new QLineEdit;
  password->setEchoMode(QLineEdit::Password);
  auto* mic = new QCheckBox("入会时开启麦克风");
  auto* camera = new QCheckBox("入会时开启摄像头");
  auto* cameras = new QComboBox;
  for (auto& c : cameras_)
    cameras->addItem(str(c.name));
  cameras->setCurrentIndex(cameraIndex_);
  auto* refresh = button("刷新摄像头", "refreshCameras");
  connect(refresh, &QPushButton::clicked, d, [this, cameras] {
    refreshDevices();
    cameras->clear();
    for (auto& c : cameras_)
      cameras->addItem(str(c.name));
  });
  auto* status = label({});
  auto* submit = button("加入会议", "primary");
  form->addRow("会议号", number);
  form->addRow("你的昵称", name);
  form->addRow("会议密码", password);
  form->addRow(mic);
  form->addRow(camera);
  form->addRow("摄像头", cameras);
  form->addRow(refresh);
  form->addRow(status);
  form->addRow(submit);
  connect(submit, &QPushButton::clicked, d,
          [this, d, number, name, password, mic, camera, cameras, status] {
            auto n = numberFrom(number->text());
            if (n.isEmpty() || name->text().trimmed().isEmpty()) {
              status->setText("请填写九位会议号和昵称");
              return;
            }
            if (camera->isChecked() && cameras->currentIndex() < 0) {
              status->setText("请先选择摄像头");
              return;
            }
            wantMic_ = mic->isChecked();
            wantCamera_ = camera->isChecked();
            cameraIndex_ = std::max(0, cameras->currentIndex());
            auto display = name->text().trimmed(), pass = password->text();
            d->accept();
            join(n, display, pass);
          });
  d->open();
}

void Window::join(QString number, QString name, QString password) {
  message({});
  if (api_.token.isEmpty() && api_.guest.isEmpty()) {
    api_.request("/api/v1/conference/guest", {},
                 [this, number, name, password](QJsonValue v, QString e) {
                   if (!e.isEmpty()) {
                     message(e);
                     return;
                   }
                   api_.guest = v.toObject().value("guest_token").toString();
                   join(number, name, password);
                 });
    return;
  }
  api_.cancel();
  polling_ = false;
  pages_->setCurrentWidget(waiting_);
  waitingText_->setText("正在提交入会申请…");
  api_.request("/api/v1/conference/join",
               {{"number", number}, {"name", name}, {"password", password}},
               [this](QJsonValue v, QString e) {
                 if (!e.isEmpty()) {
                   message(e);
                   showHome();
                   return;
                 }
                 meetingId_ = v.toObject().value("id").toString();
                 business_ = {};
                 lastUnmute_ = 0;
                 videoPage_ = 0;
                 poll();
               });
}

void Window::poll() {
  if (meetingId_.isEmpty() || polling_ || leaving_ || closing_)
    return;
  polling_ = true;
  api_.request(path("presence"), {}, [this](QJsonValue v, QString e) {
    polling_ = false;
    if (!e.isEmpty()) {
      message(e);
      return;
    }
    applyBusiness(v.toObject());
  });
}

void Window::applyBusiness(QJsonObject state) {
  if (state.value("id") == business_.value("id") &&
      state.value("version").toDouble() < business_.value("version").toDouble())
    return;
  business_ = state;
  if (meetingId_.isEmpty())
    meetingId_ = state.value("id").toString();
  auto status = state.value("status").toString(), phase = state.value("state").toString();
  if (phase == "ending" || phase == "ended" || phase == "cancelled" || status == "removed" ||
      status == "rejected" || status == "left") {
    message(status == "removed" ? "你已被主持人移出会议"
            : phase == "ending" ? "会议正在结束"
                                : "会议已结束或入会已取消");
    session_.stop();
    leaving_ = true;
    if (!running_)
      pages_->setCurrentWidget(home_);
    return;
  }
  meetingTitle_->setText(state.value("title").toString() + "  ·  " +
                         state.value("number").toString());
  auto selectedMember =
      members_->currentItem() ? members_->currentItem()->data(Qt::UserRole).toString() : QString();
  members_->clear();
  auto self = state.value("self").toString();
  for (auto value : state.value("members").toArray()) {
    auto p = value.toObject();
    QString text = p.value("name").toString();
    if (p.value("id") == state.value("host"))
      text += "  · 主持人";
    if (p.value("id").toString() == self)
      text += "（我）";
    text += p.value("status") == "waiting" ? "\n等待入会"
            : p.value("online").toBool()   ? "\n在线"
                                           : "\n尚未连接";
    auto* item = new QListWidgetItem(text, members_);
    item->setData(Qt::UserRole, p.value("id").toString());
    if (p.value("id").toString() == selectedMember)
      members_->setCurrentItem(item);
    if (p.value("id").toString() == self) {
      auto request = qint64(p.value("unmute_request").toDouble());
      if (request > lastUnmute_) {
        lastUnmute_ = request;
        if (running_ &&
            QMessageBox::question(this, "主持人请求", "主持人邀请你开启麦克风，是否开启？") ==
                QMessageBox::Yes) {
          bool found = false;
          if (auto local = media_.participants.find(media_.local_sid);
              local != media_.participants.end())
            for (auto& t : local->second.tracks)
              if (t.source == demo::rtc::PublicationSource::microphone) {
                found = true;
                if (t.muted)
                  session_.set_muted(t.sid, false);
              }
          if (!found)
            session_.enable_microphone();
        }
      }
    }
  }
  manage_->setText(isHost() ? "管理成员" : "成员");
  for (auto v : state.value("operations").toArray()) {
    auto op = v.toObject();
    if (op.value("state") == "retrying") {
      message("会议操作尚未完成，服务器正在重试");
      break;
    }
  }
  if (status == "waiting" || phase == "scheduled") {
    pages_->setCurrentWidget(waiting_);
    waitingText_->setText(phase == "scheduled" ? "正在等待主持人开始会议"
                                               : "已进入等候室，请等待主持人批准");
    return;
  }
  pages_->setCurrentWidget(meeting_);
  if (running_ && session_.snapshot().finished && !connecting_ && !leaving_ &&
      mediaError_.isEmpty())
    connectMedia();
  if (!pendingShare_.isEmpty()) {
    bool complete = false, failed = false;
    for (auto v : state.value("operations").toArray()) {
      auto op = v.toObject();
      if (op.value("kind") == "share-on" && op.value("member").toString() == self) {
        complete = op.value("state") == "done";
        failed = op.value("state") == "retrying";
      }
    }
    if (failed)
      message("共享授权处理中，媒体服务暂时不可用");
    if (complete) {
      auto id = pendingShare_;
      pendingShare_.clear();
      for (auto& s : screens_)
        if (str(s.id) == id) {
          demo::sdk::MediaSpec spec;
          spec.capture = demo::sdk::CaptureKind::screen;
          spec.screen_id = s.id;
          auto size = demo::sdk::screen_capture_size(s.width, s.height);
          spec.width = size.width;
          spec.height = size.height;
          spec.fps = 15;
          spec.local_preview = true;
          if (!session_.add_video(spec))
            message("暂时无法开始共享，请稍后重试");
        }
    }
  }
}

void Window::connectMedia() {
  connecting_ = true;
  api_.request(path("credentials"), {}, [this](QJsonValue value, QString error) {
    connecting_ = false;
    if (!error.isEmpty()) {
      message(error);
      return;
    }
    auto v = value.toObject();
    demo::SessionOptions o;
    o.connection.url = v.value("media_url").toString().toStdString();
    o.connection.token = v.value("media_token").toString().toStdString();
    o.room = v.value("room").toString().toStdString();
    o.audio_playout = true;
    o.microphone = wantMic_;
    if (wantCamera_) {
      demo::sdk::MediaSpec spec;
      spec.capture = demo::sdk::CaptureKind::camera;
      spec.device_index = cameraIndex_;
      spec.local_preview = true;
      o.media.push_back(spec);
    }
    auto generation = api_.epoch();
    o.connection.token_provider = [this, generation](demo::sdk::TokenRequest request,
                                                     demo::sdk::TokenCompletion complete) {
      QMetaObject::invokeMethod(
          &api_,
          [this, generation, request, complete = std::move(complete)] {
            if (generation != api_.epoch() || request.stop.stop_requested()) {
              complete({{}, demo::sdk::TokenFailure::denied});
              return;
            }
            api_.request(path("credentials"), {}, [complete, request](QJsonValue v, QString e) {
              if (request.stop.stop_requested() || !e.isEmpty())
                complete({{}, demo::sdk::TokenFailure::denied});
              else
                complete({v.toObject().value("media_token").toString().toStdString(),
                          demo::sdk::TokenFailure::none});
            });
          },
          Qt::QueuedConnection);
    };
    try {
      session_.start(std::move(o));
    } catch (const std::exception& e) {
      mediaError_ = str(e.what());
      message(mediaError_);
    }
  });
}

void Window::applyMedia(const demo::Snapshot& snapshot) {
  media_ = snapshot;
  if (!meetingId_.isEmpty()) {
    if (snapshot.state == demo::State::Connected && !elapsed_.isValid())
      elapsed_.start();
    QString clock;
    if (elapsed_.isValid()) {
      auto seconds = elapsed_.elapsed() / 1000;
      clock = QString(" · %1:%2")
                  .arg(seconds / 60, 2, 10, QChar('0'))
                  .arg(seconds % 60, 2, 10, QChar('0'));
    }
    connection_->setText(str(demo::state_name(snapshot.state)) + clock);
    if (!snapshot.error.empty() && mediaError_ != str(snapshot.error)) {
      mediaError_ = str(snapshot.error);
      message(mediaError_ + "；可离会后重新加入");
    }
    if (!snapshot.video_error.empty())
      message(str(snapshot.video_error));
    if (!snapshot.chat_error.empty())
      message(str(snapshot.chat_error));
  }
  bool enabled = snapshot.state == demo::State::Connected && !snapshot.changing_video;
  mic_->setEnabled(enabled);
  camera_->setEnabled(enabled);
  share_->setEnabled(enabled && pendingShare_.isEmpty());
  chatInput_->setEnabled(snapshot.state == demo::State::Connected);
  bool microphone = false, camera = false, sharing = false;
  if (auto p = snapshot.participants.find(snapshot.local_sid); p != snapshot.participants.end()) {
    for (auto& t : p->second.tracks) {
      if (t.source == demo::rtc::PublicationSource::microphone)
        microphone = !t.muted;
      if (t.source == demo::rtc::PublicationSource::camera)
        camera = !t.muted;
      if (t.source == demo::rtc::PublicationSource::screen)
        sharing = true;
    }
  }
  mic_->setText(microphone ? "静音" : "开启麦克风");
  camera_->setText(camera ? "关闭摄像头" : "开启摄像头");
  share_->setText(sharing ? "停止共享" : "共享屏幕");
  QString text;
  for (auto& m : snapshot.chat.messages()) {
    text += str(m.name.empty() ? m.sender : m.name) + "\n" + str(m.text) + "\n";
    if (m.status != demo::ChatStatus::Received)
      text += (m.status == demo::ChatStatus::Pending ? QString("发送中")
               : m.status == demo::ChatStatus::Sent  ? QString("已发送")
                                                     : QString("发送结果未确认")) +
              "\n";
    text += "\n";
  }
  if (chatStamp_ != text) {
    chatStamp_ = text;
    chatLog_->setPlainText(text);
    auto c = chatLog_->textCursor();
    c.movePosition(QTextCursor::End);
    chatLog_->setTextCursor(c);
  }
  updateVideos();
}

void Window::updateVideos() {
  auto tiles = demo::tiles(media_);
  if (auto local = media_.participants.find(media_.local_sid); local != media_.participants.end()) {
    bool preview = false;
    for (auto& t : tiles)
      if (t.id.starts_with("local-"))
        preview = true;
    if (!preview)
      tiles.insert(tiles.begin(),
                   {local->second.sid,
                    local->second.name.empty() ? "我" : local->second.name + "（我）",
                    "未开启视频",
                    {}});
  }
  if (tiles.empty())
    tiles.push_back({"self", user_.value("name").toString("我").toStdString(), "未开启视频", {}});
  auto mode = layout_->currentIndex();
  if (mode == 2) {
    std::string shared;
    for (auto& [_, p] : media_.participants)
      for (auto& t : p.tracks)
        if (t.source == demo::rtc::PublicationSource::screen)
          shared = p.sid == media_.local_sid ? t.local_id : t.sid;
    std::stable_sort(tiles.begin(), tiles.end(),
                     [&](auto& a, auto& b) { return a.id == shared && b.id != shared; });
  } else if (mode == 1) {
    std::string active;
    float level = 0;
    for (auto& [_, p] : media_.participants)
      if (p.audio_level > level) {
        level = p.audio_level;
        for (const auto& t : p.tracks)
          if (t.kind == demo::rtc::PublicationKind::video &&
              t.source != demo::rtc::PublicationSource::screen) {
            active = p.sid == media_.local_sid ? t.local_id : t.sid;
            break;
          }
      }
    std::stable_sort(tiles.begin(), tiles.end(),
                     [&](auto& a, auto& b) { return a.id == active && b.id != active; });
  }
  int count = mode == 0 ? 9 : 1;
  int total = std::max(1, (int(tiles.size()) + count - 1) / count);
  videoPage_ = std::clamp(videoPage_, 0, total - 1);
  pageLabel_->setText(QString("%1 / %2").arg(videoPage_ + 1).arg(total));
  FrameConverter::Frames frames;
  auto images = converter_.images();
  while (auto* item = grid_->takeAt(0))
    delete item;
  for (int i = 0; i < 9; ++i) {
    auto* video = videos_[i];
    int index = videoPage_ * count + i;
    if (i >= count || index >= int(tiles.size())) {
      video->hide();
      continue;
    }
    auto& t = tiles[index];
    if (t.frame)
      frames[t.id] = t.frame;
    QImage image;
    if (t.frame && images.contains(t.id))
      image = images.at(t.id);
    video->present(image, str(t.label), str(t.status), mode == 1);
    grid_->addWidget(video, i / 3, i % 3);
    video->show();
  }
  converter_.submit(std::move(frames), mode == 0 ? QSize(640, 360) : QSize(1280, 720));
}

void Window::command(QString action, QJsonObject data, std::function<void()> done) {
  api_.request(path(action), data, [this, done = std::move(done)](QJsonValue, QString e) {
    if (!e.isEmpty()) {
      message(e);
      return;
    }
    message({});
    if (done)
      done();
    poll();
  });
}

void Window::toggleMicrophone() {
  if (auto p = media_.participants.find(media_.local_sid); p != media_.participants.end()) {
    for (auto& t : p->second.tracks)
      if (t.source == demo::rtc::PublicationSource::microphone) {
        session_.set_muted(t.sid, !t.muted);
        return;
      }
  }
  if (!session_.enable_microphone())
    message("麦克风正在连接或当前无法开启");
}

void Window::toggleCamera() {
  if (auto p = media_.participants.find(media_.local_sid); p != media_.participants.end()) {
    for (auto& t : p->second.tracks)
      if (t.source == demo::rtc::PublicationSource::camera) {
        session_.remove_video(t.sid);
        return;
      }
  }
  if (cameras_.empty())
    refreshDevices();
  if (cameras_.empty()) {
    message("没有可用摄像头，请检查系统权限与设备");
    return;
  }
  demo::sdk::MediaSpec spec;
  spec.capture = demo::sdk::CaptureKind::camera;
  spec.device_index = cameraIndex_;
  spec.local_preview = true;
  session_.add_video(spec);
}

void Window::toggleShare() {
  if (auto p = media_.participants.find(media_.local_sid); p != media_.participants.end()) {
    for (auto& t : p->second.tracks)
      if (t.source == demo::rtc::PublicationSource::screen) {
        session_.remove_video(t.sid);
        command("share", {{"enabled", false}});
        return;
      }
  }
  refreshDevices();
  if (screens_.empty()) {
    message("未找到可共享显示器，请检查屏幕录制权限");
    return;
  }
  QMenu menu(this);
  for (auto& s : screens_) {
    auto* action = menu.addAction(str(s.name));
    connect(action, &QAction::triggered, this, [this, id = str(s.id)] {
      command("share", {{"enabled", true}}, [this, id] { pendingShare_ = id; });
    });
  }
  menu.exec(share_->mapToGlobal(QPoint(0, 0)));
}

void Window::invite() {
  auto text = QString(
                  "邀请你参加 AV 会议\n主题：%1\n会议号：%2\n时间：%3\n请在 AV "
                  "会议中粘贴此邀请加入。\n服务器：%4")
                  .arg(business_.value("title").toString(), business_.value("number").toString(),
                       timeText(business_.value("starts_at").toString()), api_.base);
  QApplication::clipboard()->setText(text);
  message("邀请已复制；如设置了会议密码，请另行告知受邀者");
}

void Window::memberMenu() {
  side_->show();
  side_->setCurrentIndex(0);
  if (!isHost())
    return;
  QMenu menu(this);
  auto* selected = members_->currentItem();
  QString target = selected ? selected->data(Qt::UserRole).toString() : QString();
  auto add = [&](QString text, QString action, QJsonObject data = {}) {
    auto* a = menu.addAction(text);
    connect(a, &QAction::triggered, this, [this, action, data] { command(action, data); });
  };
  if (!target.isEmpty()) {
    add("允许入会", "approve", {{"target", target}});
    add("拒绝入会", "reject", {{"target", target}});
    add("静音", "mute", {{"target", target}});
    add("请求开启麦克风", "request-unmute", {{"target", target}});
    add("设为主持人", "transfer", {{"target", target}});
    add("移出会议", "remove", {{"target", target}});
    menu.addSeparator();
  }
  add("全体静音", "mute-all");
  add(business_.value("locked").toBool() ? "解除锁定" : "锁定会议", "lock",
      {{"enabled", !business_.value("locked").toBool()}});
  auto* end = menu.addAction("结束会议");
  connect(end, &QAction::triggered, this, [this] { leave(true); });
  menu.exec(manage_->mapToGlobal(QPoint(0, 0)));
}

void Window::leave(bool end, bool closeAfter) {
  auto finishLeaving = [this, closeAfter] {
    session_.stop();
    leaving_ = true;
    if (closeAfter) {
      closing_ = true;
      poll_.stop();
      homeTimer_.stop();
    }
  };
  if (meetingId_.isEmpty()) {
    api_.cancel();
    showHome();
    return;
  }
  if (isHost() && business_.value("state") == "active" && !end) {
    QMessageBox box(QMessageBox::Question, "离开会议", "主持人离会前，请转让主持人或结束会议。",
                    QMessageBox::Cancel, this);
    auto* transfer = box.addButton("转让后离开", QMessageBox::ActionRole);
    auto* finish = box.addButton("结束会议", QMessageBox::DestructiveRole);
    box.exec();
    if (box.clickedButton() == finish) {
      leave(true, closeAfter);
      return;
    }
    if (box.clickedButton() == transfer) {
      QMenu menu(this);
      for (auto v : business_.value("members").toArray()) {
        auto p = v.toObject();
        if (p.value("registered").toBool() && p.value("online").toBool() &&
            p.value("id") != business_.value("self")) {
          auto* a = menu.addAction(p.value("name").toString());
          connect(a, &QAction::triggered, this,
                  [this, id = p.value("id").toString(), finishLeaving] {
                    command("transfer", {{"target", id}},
                            [this, finishLeaving] { command("leave", {}, finishLeaving); });
                  });
        }
      }
      if (menu.isEmpty())
        message("没有可接任的在线注册成员，请选择结束会议");
      else
        menu.exec(QCursor::pos());
    }
    return;
  }
  if (end && QMessageBox::question(this, "结束会议", "结束后所有成员将离开会议，确定结束？") !=
                 QMessageBox::Yes)
    return;
  command(end ? "end" : "leave", {}, finishLeaving);
}

void Window::exitMeeting() {
  api_.cancel();
  elapsed_.invalidate();
  meetingId_.clear();
  business_ = {};
  polling_ = false;
  connecting_ = false;
  leaving_ = false;
  wantCamera_ = wantMic_ = false;
  mediaError_.clear();
  pendingShare_.clear();
  converter_.clear();
  media_ = {};
  chatInput_->clear();
  chatLog_->clear();
  showHome();
}

void Window::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  if (!toolbar_)
    return;
  while (auto* item = toolbar_->takeAt(0))
    delete item;
  const int columns = width() >= 1060 ? 7 : 4;
  for (size_t i = 0; i < toolbarButtons_.size(); ++i)
    toolbar_->addWidget(toolbarButtons_[i], int(i) / columns, int(i) % columns);
}

void Window::closeEvent(QCloseEvent* event) {
  if (running_ && !meetingId_.isEmpty() && !closing_ && !leaving_) {
    event->ignore();
    leave(false, true);
    return;
  }
  if (!session_.snapshot().finished) {
    event->ignore();
    if (!closing_) {
      closing_ = true;
      api_.cancel();
      poll_.stop();
      homeTimer_.stop();
      session_.stop();
      message("正在释放会议资源…");
    }
    return;
  }
  api_.cancel();
  if (running_) {
    QSettings p;
    p.setValue("geometry", saveGeometry());
  }
  event->accept();
}

void Window::showDetails(QListWidget* list) {
  auto* item = list->currentItem();
  if (!item)
    return;
  auto m = item->data(Qt::UserRole).toJsonObject();
  if (m.isEmpty())
    return;
  auto* d = new QDialog(this);
  d->setAttribute(Qt::WA_DeleteOnClose);
  d->setWindowTitle("会议详情");
  auto* l = new QVBoxLayout(d);
  l->addWidget(label(m.value("title").toString(), "heading"));
  l->addWidget(label("会议号：" + m.value("number").toString() + "\n时间：" +
                     timeText(m.value("starts_at").toString()) + "\n状态：" +
                     stateName(m.value("state").toString())));
  auto* copy = button("复制邀请", "copyInvite");
  l->addWidget(copy);
  connect(copy, &QPushButton::clicked, d, [this, m] {
    auto old = business_;
    business_ = m;
    invite();
    business_ = old;
  });
  auto state = m.value("state").toString();
  if (state == "active" || state == "scheduled") {
    auto* enter = button(
        m.value("owner") == user_.value("id") && state == "scheduled" ? "开始会议" : "加入会议",
        "primary");
    l->addWidget(enter);
    connect(enter, &QPushButton::clicked, d, [this, d, m] {
      d->accept();
      if (m.value("owner") == user_.value("id") && m.value("state") == "scheduled") {
        api_.request("/api/v1/conference/meetings/" + m.value("id").toString() + "/start", {},
                     [this, m](QJsonValue, QString e) {
                       if (!e.isEmpty()) {
                         message(e);
                         return;
                       }
                       join(m.value("number").toString(), user_.value("name").toString(), {});
                     });
      } else {
        join(m.value("number").toString(), user_.value("name").toString(), {});
      }
    });
  }
  if (state == "scheduled" && m.value("owner") == user_.value("id")) {
    auto* edit = button("编辑预约", "editMeeting");
    auto* cancel = button("取消预约", "danger");
    l->addWidget(edit);
    l->addWidget(cancel);
    connect(edit, &QPushButton::clicked, d, [this, d, m] {
      d->accept();
      schedule(m);
    });
    connect(cancel, &QPushButton::clicked, d, [this, d, m] {
      if (QMessageBox::question(d, "取消预约", "确认取消这场预约？") != QMessageBox::Yes)
        return;
      api_.request("/api/v1/conference/meetings/" + m.value("id").toString() + "/cancel", {},
                   [this](QJsonValue, QString e) {
                     message(e);
                     refreshHome();
                   });
      d->accept();
    });
  }
  d->open();
}
}  // namespace meeting
