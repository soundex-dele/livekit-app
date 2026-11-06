#include "livekit/compat/algorithm.hpp"
#include "window.hpp"
#include <av/ui.hpp>
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
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace meeting {
namespace {
QPushButton* button(QString text, QString name, QWidget* parent = nullptr) {
  auto* b = new QPushButton(text, parent);
  b->setObjectName(name);
  b->setMinimumHeight(36);
  b->setCursor(Qt::PointingHandCursor);
  av::ui::set_button_role(b, name == "primary" ? av::ui::ButtonRole::Primary
                             : name == "danger" ? av::ui::ButtonRole::Danger
                                                : av::ui::ButtonRole::Secondary);
  return b;
}

QPushButton* add_cancel(av::ui::Dialog* dialog, const QString& text = "取消") {
  auto* cancel = dialog->add_action(text);
  QObject::connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
  return cancel;
}

// Self-drawn brand mark: no third-party logo or artwork is shipped.
QLabel* brand_mark(int size = 34) {
  auto* l = new QLabel("AV");
  l->setAlignment(Qt::AlignCenter);
  l->setFixedSize(size, size);
  l->setStyleSheet(QString("background:#2563eb;color:#ffffff;border-radius:%1px;"
                           "font-size:%2px;font-weight:600;")
                       .arg(size / 4)
                       .arg(size > 30 ? 13 : 10));
  return l;
}

QLabel* label(QString text, QString name = {}) {
  auto* l = new QLabel(text);
  l->setObjectName(name);
  l->setWordWrap(true);
  if (name == "heading") {
    av::ui::set_text_role(l, "title");
  } else if (name == "sectionTitle" || name == "brand") {
    av::ui::set_text_role(l, "heading");
  } else if (name == "subtle" || name == "meetingSubtle" || name == "meetingNumber" ||
             name == "memberCount") {
    av::ui::set_text_role(l, "muted");
  }
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

QString spacedNumber(QString number) {
  QString out;
  for (int i = 0; i < number.size(); ++i) {
    if (i && i % 3 == 0)
      out += ' ';
    out += number.at(i);
  }
  return out;
}
}  // namespace

Window::Window(bool running) : running_(running) {
  setObjectName("conferenceWindow");
  setWindowTitle("AV 会议");
  resize(1100, 740);
  setMinimumSize(820, 560);
  QSettings prefs;
  api_.base = prefs.value("server", "http://127.0.0.1:8080").toString();
  cameraIndex_ = prefs.value("camera", 0).toInt();
  if (running_)
    restoreGeometry(prefs.value("geometry").toByteArray());
  av::ui::apply_theme(this, av::ui::Theme::Light);
  setStyleSheet(ui::style_sheet());

  // Qt only paints stylesheet backgrounds for plain QWidgets when this flag
  // is set; every full-bleed surface below goes through the helper.
  auto surface = [](QString name) {
    auto* w = new QWidget;
    w->setObjectName(name);
    w->setAttribute(Qt::WA_StyledBackground, true);
    if (name == "listCard" || name == "waitingCard") {
      av::ui::set_surface(w);
    } else if (name == "navRail") {
      av::ui::set_surface(w, "sidebar");
    } else if (name == "meetingTopBar") {
      av::ui::set_surface(w, "toolbar");
    } else if (name == "videoStage" || name == "meetingToolbar") {
      av::ui::apply_theme(w, av::ui::Theme::Dark);
      av::ui::set_surface(w, name == "videoStage" ? "media" : "toolbar");
    }
    return w;
  };
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

  // Home: navigation rail plus card-based meeting actions.
  home_ = surface("homePage");
  auto* home = new QHBoxLayout(home_);
  home->setContentsMargins(0, 0, 0, 0);
  home->setSpacing(0);
  auto* rail = surface("navRail");
  rail->setFixedWidth(80);
  auto* railLayout = new QVBoxLayout(rail);
  railLayout->setContentsMargins(0, 14, 6, 14);
  railLayout->setSpacing(4);
  railLayout->addWidget(brand_mark(36), 0, Qt::AlignHCenter);
  railLayout->addSpacing(20);
  auto* navMeetings = new ui::NavButton("会议", ui::Glyph::Home, "navMeetings", rail);
  auto* navHistory = new ui::NavButton("历史", ui::Glyph::Clock, "navHistory", rail);
  railLayout->addWidget(navMeetings, 0, Qt::AlignHCenter);
  railLayout->addWidget(navHistory, 0, Qt::AlignHCenter);
  railLayout->addStretch(1);
  auto* navSettings = new ui::NavButton("设置", ui::Glyph::Gear, "settingsButton", rail);
  accountButton_ = new ui::NavButton("登录", ui::Glyph::Members, "accountButton", rail);
  railLayout->addWidget(navSettings, 0, Qt::AlignHCenter);
  railLayout->addWidget(accountButton_, 0, Qt::AlignHCenter);
  home->addWidget(rail);

  auto* content = new QWidget;
  auto* c = new QVBoxLayout(content);
  c->setContentsMargins(24, 24, 24, 20);
  c->setSpacing(16);
  auto* headline = new QHBoxLayout;
  headline->addWidget(label("AV 会议", "brand"));
  headline->addStretch(1);
  account_ = label("游客", "subtle");
  headline->addWidget(account_);
  headline->addSpacing(14);
  headline->addWidget(label(QDateTime::currentDateTime().toString("M月d日"), "subtle"));
  c->addLayout(headline);
  c->addSpacing(2);
  c->addWidget(label("让每一次交流，更进一步", "heading"));
  c->addWidget(label("连接团队、分享想法，从这里开始。", "subtle"));
  auto* workspace = new QHBoxLayout;
  workspace->setSpacing(20);
  auto* launchPanel = new QWidget;
  launchPanel->setObjectName("meetingLaunchPanel");
  launchPanel->setFixedWidth(224);
  auto* actions = new QVBoxLayout(launchPanel);
  actions->setContentsMargins(0, 0, 0, 0);
  actions->setSpacing(12);
  actions->addWidget(label("开始交流", "sectionTitle"));
  auto* add = new ui::ActionCard("加入会议", ui::Glyph::Plus, true);
  add->setObjectName("joinButton");
  auto* quick = new ui::ActionCard("快速会议", ui::Glyph::Video, false);
  quick->setObjectName("quickButton");
  auto* plan = new ui::ActionCard("预定会议", ui::Glyph::Calendar, false);
  plan->setObjectName("scheduleButton");
  for (auto* b : {add, quick, plan}) {
    actions->addWidget(b);
  }
  actions->addStretch(1);
  workspace->addWidget(launchPanel);
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
  auto* agenda = surface("listCard");
  auto* agendaLayout = new QVBoxLayout(agenda);
  agendaLayout->setContentsMargins(20, 18, 20, 16);
  agendaLayout->setSpacing(12);
  agendaLayout->addWidget(label("我的会议", "sectionTitle"));
  agendaLayout->addWidget(label("查看日程，继续与团队的交流", "subtle"));
  homeTabs_ = new QTabWidget;
  homeTabs_->setObjectName("homeTabs");
  upcoming_ = new QListWidget;
  upcoming_->setObjectName("upcomingMeetings");
  history_ = new QListWidget;
  history_->setObjectName("meetingHistory");
  upcoming_->addItem("登录后创建会议，或直接加入会议");
  history_->addItem("暂无历史会议");
  homeTabs_->addTab(upcoming_, "即将开始");
  homeTabs_->addTab(history_, "历史会议");
  agendaLayout->addWidget(homeTabs_, 1);
  workspace->addWidget(agenda, 1);
  c->addLayout(workspace, 1);
  c->addWidget(label("默认关闭摄像头和麦克风 · 单场会议最多 25 人", "subtle"));
  home->addWidget(content, 1);
  navMeetings->set_selected(true);
  connect(navMeetings, &QPushButton::clicked, this, [this, navMeetings, navHistory] {
    homeTabs_->setCurrentIndex(0);
    navMeetings->set_selected(true);
    navHistory->set_selected(false);
  });
  connect(navHistory, &QPushButton::clicked, this, [this, navMeetings, navHistory] {
    homeTabs_->setCurrentIndex(1);
    navMeetings->set_selected(false);
    navHistory->set_selected(true);
  });
  connect(navSettings, &QPushButton::clicked, this, &Window::settings);
  connect(accountButton_, &QPushButton::clicked, this, &Window::login);
  connect(upcoming_, &QListWidget::itemDoubleClicked, this, [this] { showDetails(upcoming_); });
  connect(history_, &QListWidget::itemDoubleClicked, this, [this] { showDetails(history_); });
  pages_->addWidget(home_);

  // Waiting room: a centered card, matching the pre-meeting light theme.
  waiting_ = surface("waitingPage");
  auto* w = new QVBoxLayout(waiting_);
  w->addStretch(1);
  auto* waitCard = surface("waitingCard");
  waitCard->setFixedWidth(420);
  auto* cv = new QVBoxLayout(waitCard);
  cv->setContentsMargins(34, 30, 34, 28);
  cv->setSpacing(12);
  auto* waitIcon = new QLabel;
  waitIcon->setPixmap(
      ui::glyph(ui::Glyph::Shield, QColor("#2563eb"), 48).pixmap(QSize(48, 48)));
  waitIcon->setFixedHeight(56);
  waitIcon->setAlignment(Qt::AlignCenter);
  cv->addWidget(waitIcon, 0, Qt::AlignHCenter);
  auto* waitTitle = label("会议即将开始", "heading");
  waitTitle->setAlignment(Qt::AlignCenter);
  cv->addWidget(waitTitle);
  waitingText_ = label("正在提交入会申请…", "waitingText");
  waitingText_->setAlignment(Qt::AlignCenter);
  cv->addWidget(waitingText_);
  cv->addSpacing(8);
  auto* cancel = button("取消入会", "cancelJoin");
  cv->addWidget(cancel, 0, Qt::AlignHCenter);
  connect(cancel, &QPushButton::clicked, this, [this] { leave(); });
  w->addWidget(waitCard, 0, Qt::AlignHCenter);
  w->addStretch(1);
  pages_->addWidget(waiting_);

  // In-meeting: dark chrome, video stage on a black canvas, collapsible side
  // drawer and an icon-over-label toolbar.
  meeting_ = surface("meetingPage");
  auto* meet = new QVBoxLayout(meeting_);
  meet->setContentsMargins(0, 0, 0, 0);
  meet->setSpacing(0);
  auto* topbar = surface("meetingTopBar");
  topbar->setFixedHeight(52);
  auto* tb = new QHBoxLayout(topbar);
  tb->setContentsMargins(16, 0, 14, 0);
  tb->setSpacing(10);
  tb->addWidget(brand_mark(26));
  meetingTitle_ = label("AV 会议", "meetingTitle");
  meetingTitle_->setWordWrap(false);
  meetingTitle_->setMaximumWidth(340);
  tb->addWidget(meetingTitle_);
  meetingNumber_ = label({}, "meetingNumber");
  meetingNumber_->setWordWrap(false);
  tb->addWidget(meetingNumber_);
  connection_ = label("正在连接", "meetingSubtle");
  connection_->setWordWrap(false);
  connection_->setMinimumWidth(112);
  memberCount_ = label({}, "memberCount");
  memberCount_->setWordWrap(false);
  memberCount_->setMinimumWidth(52);
  tb->addStretch(1);
  tb->addWidget(connection_);
  tb->addWidget(memberCount_);
  layout_ = new QComboBox;
  layout_->setObjectName("layoutMode");
  layout_->addItems({"宫格视图", "演讲者视图", "共享优先"});
  connect(layout_, &QComboBox::currentIndexChanged, this, [this] {
    videoPage_ = 0;
    updateVideos();
  });
  tb->addWidget(layout_);
  meet->addWidget(topbar);

  auto* body = new QHBoxLayout;
  body->setContentsMargins(0, 0, 0, 0);
  body->setSpacing(0);
  auto* stage = surface("videoStage");
  auto* sv = new QVBoxLayout(stage);
  sv->setContentsMargins(12, 12, 12, 8);
  sv->setSpacing(8);
  auto* canvas = new QWidget;
  grid_ = new QGridLayout(canvas);
  grid_->setContentsMargins(0, 0, 0, 0);
  grid_->setSpacing(8);
  sv->addWidget(canvas, 1);
  for (int i = 0; i < 9; i++) {
    auto* tile = new VideoWidget(canvas);
    videos_.push_back(tile);
    tile->hide();
  }
  auto* pagination = new QHBoxLayout;
  pagination->setSpacing(8);
  pagination->addStretch(1);
  auto* prev = button({}, "previousPage");
  prev->setToolTip("上一页");
  prev->setFixedSize(28, 28);
  prev->setStyleSheet("padding:0; min-height:0;");
  prev->setIcon(ui::glyph(ui::Glyph::ChevronLeft, QColor("#e7e9ee"), 18));
  auto* next = button({}, "nextPage");
  next->setToolTip("下一页");
  next->setFixedSize(28, 28);
  next->setStyleSheet("padding:0; min-height:0;");
  next->setIcon(ui::glyph(ui::Glyph::ChevronRight, QColor("#e7e9ee"), 18));
  pageLabel_ = label("1 / 1", "pageLabel");
  pagination->addWidget(prev);
  pagination->addWidget(pageLabel_);
  pagination->addWidget(next);
  pagination->addStretch(1);
  sv->addLayout(pagination);
  connect(prev, &QPushButton::clicked, this, [this] {
    videoPage_ = std::max(0, videoPage_ - 1);
    updateVideos();
  });
  connect(next, &QPushButton::clicked, this, [this] {
    ++videoPage_;
    updateVideos();
  });
  body->addWidget(stage, 1);

  side_ = new QTabWidget;
  side_->setObjectName("meetingSidePanel");
  side_->setAttribute(Qt::WA_StyledBackground, true);
  side_->setFixedWidth(288);
  members_ = new QListWidget;
  members_->setObjectName("memberList");
  side_->addTab(members_, "成员");
  auto* chat = new QWidget;
  auto* chatLayout = new QVBoxLayout(chat);
  chatLayout->setContentsMargins(10, 10, 10, 10);
  chatLayout->setSpacing(8);
  chatLog_ = new QPlainTextEdit;
  chatLog_->setReadOnly(true);
  chatLog_->setObjectName("chatHistory");
  chatInput_ = new QPlainTextEdit;
  chatInput_->setObjectName("chatInput");
  chatInput_->setPlaceholderText("发送给所有人，Ctrl+Enter 发送");
  chatInput_->setFixedHeight(72);
  auto* send = button("发送", "sendChat");
  av::ui::set_button_role(send, av::ui::ButtonRole::Primary);
  auto* sendRow = new QHBoxLayout;
  sendRow->addStretch(1);
  sendRow->addWidget(send);
  chatLayout->addWidget(chatLog_, 1);
  chatLayout->addWidget(chatInput_);
  chatLayout->addLayout(sendRow);
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
  body->addWidget(side_);
  meet->addLayout(body, 1);

  auto* toolbar = surface("meetingToolbar");
  toolbar->setFixedHeight(78);
  auto* tl = new QHBoxLayout(toolbar);
  tl->setContentsMargins(16, 0, 16, 0);
  tl->setSpacing(4);
  tl->addStretch(1);
  mic_ = new ui::ToolButton("静音", ui::Glyph::Mic);
  mic_->setObjectName("microphoneButton");
  camera_ = new ui::ToolButton("关闭视频", ui::Glyph::Camera);
  camera_->setObjectName("cameraButton");
  share_ = new ui::ToolButton("共享屏幕", ui::Glyph::Share);
  share_->setObjectName("shareButton");
  auto* inviteButton = new ui::ToolButton("邀请", ui::Glyph::Invite);
  inviteButton->setObjectName("inviteButton");
  manage_ = new ui::ToolButton("成员", ui::Glyph::Members);
  manage_->setObjectName("manageButton");
  auto* chatButton = new ui::ToolButton("聊天", ui::Glyph::Chat);
  chatButton->setObjectName("chatButton");
  auto* leaveButton = new ui::ToolButton("离开", ui::Glyph::Leave);
  leaveButton->setObjectName("leaveButton");
  leaveButton->set_danger(true);
  for (auto* b : {mic_, camera_, share_, inviteButton, manage_, chatButton}) {
    toolbarButtons_.push_back(b);
    tl->addWidget(b);
  }
  tl->addStretch(1);
  auto* separator = surface("toolbarSeparator");
  separator->setFixedSize(1, 36);
  separator->setStyleSheet("background:#3a3e47;");
  tl->addWidget(separator);
  tl->addSpacing(12);
  tl->addWidget(leaveButton);
  toolbarButtons_.push_back(leaveButton);
  meet->addWidget(toolbar);
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
    if (av::ui::MessageBox::question(this, "退出登录", "退出当前账号？") == QMessageBox::Yes) {
      api_.request("/api/v1/auth/logout", {}, [this](QJsonValue, QString e) {
        if (!e.isEmpty()) {
          message(e);
          return;
        }
        api_.cancel();
        api_.token.clear();
        user_ = {};
        account_->setText("游客");
        accountButton_->setText("登录");
        refreshHome();
      });
    }
    return;
  }
  auto* d = new av::ui::Dialog(this, "登录 AV 会议");
  d->setAttribute(Qt::WA_DeleteOnClose);
  connect(d, &QDialog::rejected, this, [this] { api_.cancel(); });
  auto* form = new QFormLayout;
  form->setSpacing(12);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setRowWrapPolicy(QFormLayout::WrapLongRows);
  d->content_layout()->addLayout(form);
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
  add_cancel(d);
  d->button_layout()->addWidget(reg);
  d->button_layout()->addWidget(submit);
  auto run = [this, d, name, password, status, submit, reg](QString action) {
    submit->setEnabled(false);
    reg->setEnabled(false);
    d->body()->setEnabled(false);
    d->set_error({});
    status->setText("正在验证账号…");
    QPointer<av::ui::Dialog> guard = d;
    api_.request("/api/v1/auth/" + action, {{"name", name->text()}, {"password", password->text()}},
                 [this, guard, status, submit, reg](QJsonValue v, QString e) {
                   if (!guard || !guard->isVisible())
                     return;
                   guard->body()->setEnabled(true);
                   submit->setEnabled(true);
                   reg->setEnabled(true);
                   status->setText("用户名 3–32 位，密码至少 10 字节");
                   if (!e.isEmpty()) {
                     guard->set_error(e);
                     return;
                   }
                   api_.token = v.toObject().value("session_token").toString();
                   api_.guest.clear();
                   user_ = v.toObject().value("user").toObject();
                   account_->setText(user_.value("name").toString());
                   accountButton_->setText("账号");
                   guard->accept();
                   message({});
                   api_.request(
                       "/api/v1/organizations", {},
                       [this](QJsonValue list, QString error) {
                         if (!error.isEmpty()) {
                           message(error);
                           return;
                         }
                         const auto organizations = list.toObject().value("items").toArray();
                         if (organizations.isEmpty()) {
                           bool accepted = false;
                           const auto name = av::ui::InputDialog::getText(
                               this, "创建组织", "组织名称", QLineEdit::Normal, {}, &accepted);
                           if (!accepted || name.trimmed().isEmpty()) {
                             message("创建会议前需要先加入或创建组织");
                             return;
                           }
                           api_.request("/api/v1/organizations", {{"name", name.trimmed()}},
                                        [this](QJsonValue value, QString e) {
                                          if (!e.isEmpty()) {
                                            message(e);
                                            return;
                                          }
                                          api_.organization =
                                              value.toObject().value("id").toString();
                                          api_.request("/api/v1/av/organizations/initialize", {},
                                                       [this](QJsonValue, QString e) {
                                                         if (!e.isEmpty()) {
                                                           message(e);
                                                           return;
                                                         }
                                                         refreshHome();
                                                       });
                                        });
                           return;
                         }
                         int index = 0;
                         if (organizations.size() > 1) {
                           QStringList names;
                           for (const auto& value : organizations) {
                             const auto item = value.toObject();
                             names.append(item.value("data").toObject().value("name").toString() +
                                          " ? " + item.value("id").toString());
                           }
                           bool accepted = false;
                           const auto selected = av::ui::InputDialog::getItem(
                               this, "选择组织", "请选择当前组织", names, 0, false, &accepted);
                           if (!accepted) {
                             message("请先选择一个组织");
                             return;
                           }
                           index = names.indexOf(selected);
                         }
                         api_.organization = organizations[index].toObject().value("id").toString();
                         refreshHome();
                       },
                       true);
                 });
  };
  connect(submit, &QPushButton::clicked, d, [run] { run("login"); });
  connect(reg, &QPushButton::clicked, d, [run] { run("register"); });
  connect(password, &QLineEdit::returnPressed, submit, &QPushButton::click);
  d->open();
}

void Window::refreshDevices() {
  auto cameras = demo::sdk::video_capture_devices();
  auto screens = demo::sdk::screen_capture_devices();
  if (!cameras)
    message(str(cameras.error().message));
  else
    cameras_ = std::move(*cameras);
  if (!screens)
    message(str(screens.error().message));
  else
    screens_ = std::move(*screens);
  if (cameraIndex_ >= int(cameras_.size()))
    cameraIndex_ = 0;
}

void Window::settings() {
  auto* d = new av::ui::Dialog(this, "会议设置", "管理连接和音视频设备。");
  d->setAttribute(Qt::WA_DeleteOnClose);
  auto* form = new QFormLayout;
  form->setSpacing(12);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setRowWrapPolicy(QFormLayout::WrapLongRows);
  d->content_layout()->addLayout(form);
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
  buttons->button(QDialogButtonBox::Save)->setText("保存");
  buttons->button(QDialogButtonBox::Cancel)->setText("取消");
  d->button_layout()->addWidget(buttons);
  av::ui::set_button_role(buttons->button(QDialogButtonBox::Save), av::ui::ButtonRole::Primary);
  connect(buttons, &QDialogButtonBox::rejected, d, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, d, [this, d, server, devices] {
    QUrl url(server->text().trimmed());
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https") ||
        url.host().isEmpty() || !url.userInfo().isEmpty()) {
      d->set_error("请输入有效的 HTTP/HTTPS 服务地址");
      server->setFocus();
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
      accountButton_->setText("登录");
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
  auto* d = new av::ui::Dialog(this, existing.isEmpty() ? "预定会议" : "编辑预约");
  d->setAttribute(Qt::WA_DeleteOnClose);
  auto* form = new QFormLayout;
  form->setSpacing(12);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setRowWrapPolicy(QFormLayout::WrapLongRows);
  d->content_layout()->addLayout(form);
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
  add_cancel(d);
  d->button_layout()->addWidget(save);
  connect(save, &QPushButton::clicked, d,
          [this, d, title, time, duration, password, waiting, status, save, existing] {
            if (title->text().trimmed().isEmpty()) {
              d->set_error("请填写会议主题");
              title->setFocus();
              return;
            }
            save->setEnabled(false);
            d->set_busy(true);
            d->set_error({});
            status->setText("正在保存预约…");
            QJsonObject data{{"title", title->text().trimmed()},
                             {"starts_at", time->dateTime().toUTC().toString(Qt::ISODate)},
                             {"duration_minutes", duration->value()},
                             {"password", password->text()},
                             {"waiting_room", waiting->isChecked()},
                             {"scheduled", true}};
            auto url = existing.isEmpty() ? QString("/api/v1/conference/meetings")
                                          : QString("/api/v1/conference/meetings/") +
                                                existing.value("id").toString() + "/edit";
            QPointer<av::ui::Dialog> guard = d;
            api_.request(url, data, [this, guard, status, save](QJsonValue, QString e) {
              if (!guard || !guard->isVisible())
                return;
              guard->set_busy(false);
              save->setEnabled(true);
              status->clear();
              if (!e.isEmpty()) {
                guard->set_error(e);
                return;
              }
              guard->accept();
              refreshHome();
            });
          });
  d->open();
}

void Window::joinDialog() {
  auto* d = new av::ui::Dialog(this, "加入会议");
  d->setAttribute(Qt::WA_DeleteOnClose);
  auto* form = new QFormLayout;
  form->setSpacing(12);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setRowWrapPolicy(QFormLayout::WrapLongRows);
  d->content_layout()->addLayout(form);
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
  add_cancel(d);
  d->button_layout()->addWidget(submit);
  connect(d, &QDialog::rejected, this, [this, d] {
    if (d->property("joining").toBool()) {
      api_.cancel();
      showHome();
    }
  });
  connect(submit, &QPushButton::clicked, d,
          [this, d, number, name, password, mic, camera, cameras, status, submit] {
            auto n = numberFrom(number->text());
            if (n.isEmpty() || name->text().trimmed().isEmpty()) {
              d->set_error("请填写九位会议号和昵称");
              (n.isEmpty() ? number : name)->setFocus();
              return;
            }
            if (camera->isChecked() && cameras->currentIndex() < 0) {
              d->set_error("请先选择摄像头");
              cameras->setFocus();
              return;
            }
            wantMic_ = mic->isChecked();
            wantCamera_ = camera->isChecked();
            cameraIndex_ = std::max(0, cameras->currentIndex());
            auto display = name->text().trimmed(), pass = password->text();
            d->setProperty("joining", true);
            d->set_error({});
            d->body()->setEnabled(false);
            submit->setEnabled(false);
            status->setText("正在提交入会申请…");
            QPointer<av::ui::Dialog> guard = d;
            join(n, display, pass, [guard, submit, status](QString error) {
              if (!guard || !guard->isVisible()) {
                return;
              }
              guard->setProperty("joining", false);
              guard->body()->setEnabled(true);
              submit->setEnabled(true);
              status->clear();
              if (!error.isEmpty()) {
                guard->set_error(error);
                return;
              }
              guard->accept();
            });
          });
  d->open();
}

void Window::join(QString number, QString name, QString password,
                  std::function<void(QString)> on_complete) {
  message({});
  api_.cancel();
  api_.guest.clear();
  api_.media_policy = 0;
  polling_ = false;
  pages_->setCurrentWidget(waiting_);
  waitingText_->setText("正在提交入会申请…");
  api_.request("/api/v1/conference/join",
               {{"number", number}, {"name", name}, {"password", password}},
               [this, on_complete = std::move(on_complete)](QJsonValue v, QString e) {
                 if (!e.isEmpty()) {
                   if (on_complete) {
                     on_complete(e);
                   }
                   message(e);
                   showHome();
                   return;
                 }
                 meetingId_ = v.toObject().value("id").toString();
                 business_ = {};
                 lastUnmute_ = 0;
                 videoPage_ = 0;
                 if (on_complete) {
                   on_complete({});
                 }
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
  const auto next_policy = qint64(state.value("self_policy").toDouble());
  if (running_ && api_.media_policy != 0 && next_policy > api_.media_policy) {
    api_.media_policy = 0;
    policyRestart_ = true;
    mediaError_.clear();
    session_.stop();
  }
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
  const QString title = state.value("title").toString();
  meetingTitle_->setText(fontMetrics().elidedText(title, Qt::ElideRight, 320));
  meetingTitle_->setToolTip(title);
  meetingNumber_->setText("会议号 " + spacedNumber(state.value("number").toString()));
  memberCount_->setText(QString("%1 人").arg(state.value("members").toArray().size()));
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
      if (p.value("muted").toBool()) {
        wantMic_ = false;
      }
      auto request = qint64(p.value("unmute_request").toDouble());
      if (request > lastUnmute_) {
        lastUnmute_ = request;
        if (running_ &&
            av::ui::MessageBox::question(this, "主持人请求", "主持人邀请你开启麦克风，是否开启？") ==
                QMessageBox::Yes) {
          wantMic_ = true;
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
    if (complete && session_.snapshot().state == demo::State::Connected) {
      auto id = pendingShare_;
      pendingShare_.clear();
      for (auto& s : screens_)
        if (str(s.id) == id) {
          demo::sdk::MediaSpec spec;
          spec.capture = demo::sdk::CaptureKind::screen;
          spec.screen_id = s.id;
          auto size = demo::sdk::screen_capture_size(s.width, s.height);
          if (!size) {
            message(str(size.error().message));
            continue;
          }
          spec.width = size->width;
          spec.height = size->height;
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
    const auto sources = v.value("granted_capabilities").toObject().value("sources").toArray();
    o.microphone = wantMic_ && (sources.isEmpty() || sources.contains("microphone"));
    if (wantCamera_) {
      demo::sdk::MediaSpec spec;
      spec.capture = demo::sdk::CaptureKind::camera;
      spec.device_index = cameraIndex_;
      spec.local_preview = true;
      o.media.push_back(spec);
    }
    // Room retains refreshed tokens received from SFU signaling for rejoin.
    try {
      session_.start(std::move(o));
      policyRestart_ = false;
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
    if (!policyRestart_ && !snapshot.error.empty() && mediaError_ != str(snapshot.error)) {
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
  mic_->setText(microphone ? "静音" : "解除静音");
  mic_->set_glyph(microphone ? ui::Glyph::Mic : ui::Glyph::MicOff);
  mic_->set_danger(!microphone);
  camera_->setText(camera ? "关闭视频" : "开启视频");
  camera_->set_glyph(camera ? ui::Glyph::Camera : ui::Glyph::CameraOff);
  camera_->set_danger(!camera);
  share_->setText(sharing ? "结束共享" : "共享屏幕");
  share_->set_glyph(ui::Glyph::Share, sharing ? QColor("#4d94ff") : QColor("#e7e9ee"));
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
      if (livekit::compat::starts_with(t.id, "local-"))
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
    if (t.frame && images.count(t.id))
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
  for (const auto& value : business_.value("members").toArray()) {
    const auto member = value.toObject();
    if (member.value("id") == business_.value("self") && member.value("muted").toBool()) {
      message("主持人已关闭麦克风权限，请联系主持人允许开启");
      return;
    }
  }
  if (auto p = media_.participants.find(media_.local_sid); p != media_.participants.end()) {
    for (auto& t : p->second.tracks)
      if (t.source == demo::rtc::PublicationSource::microphone) {
        wantMic_ = t.muted;
        session_.set_muted(t.sid, !t.muted);
        return;
      }
  }
  wantMic_ = true;
  if (!session_.enable_microphone())
    message("麦克风正在连接或当前无法开启");
}

void Window::toggleCamera() {
  if (auto p = media_.participants.find(media_.local_sid); p != media_.participants.end()) {
    for (auto& t : p->second.tracks)
      if (t.source == demo::rtc::PublicationSource::camera) {
        wantCamera_ = false;
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
  wantCamera_ = true;
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
    connect(a, &QAction::triggered, this, [this, action, data] {
      if (action == "request-unmute") {
        command("mute", {{"target", data.value("target")}, {"enabled", false}},
                [this, data] { command("request-unmute", data); });
      } else {
        command(action, data);
      }
    });
  };
  if (!target.isEmpty()) {
    add("允许入会", "admit", {{"target", target}});
    add("拒绝入会", "reject", {{"target", target}});
    add("静音", "mute", {{"target", target}, {"enabled", true}});
    add("允许开启麦克风", "mute", {{"target", target}, {"enabled", false}});
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
    av::ui::Dialog box(this, "离开会议", "主持人离会前，请转让主持人或结束会议。");
    auto* cancel = add_cancel(&box);
    auto* transfer = box.add_action("转让后离开", av::ui::ButtonRole::Primary);
    auto* finish = box.add_action("结束会议", av::ui::ButtonRole::Danger);
    enum class Choice { Cancel, Transfer, Finish };
    Choice choice = Choice::Cancel;
    connect(transfer, &QPushButton::clicked, &box, [&] {
      choice = Choice::Transfer;
      box.accept();
    });
    connect(finish, &QPushButton::clicked, &box, [&] {
      choice = Choice::Finish;
      box.accept();
    });
    cancel->setDefault(true);
    cancel->setFocus();
    box.exec();
    if (choice == Choice::Finish) {
      leave(true, closeAfter);
      return;
    }
    if (choice == Choice::Transfer) {
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
  if (end && av::ui::MessageBox::warning(this, "结束会议", "结束后所有成员将离开会议，确定结束？",
                                       QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
                 QMessageBox::Yes) {
    return;
  }
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
  // Narrow windows keep every action reachable: icons only, smaller drawer.
  const bool compact = width() < 960;
  for (auto* item : toolbarButtons_)
    item->set_compact(compact);
  if (side_)
    side_->setFixedWidth(compact ? 230 : 288);
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
  auto* d = new av::ui::Dialog(this, "会议详情");
  d->setAttribute(Qt::WA_DeleteOnClose);
  auto* l = d->content_layout();
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
      if (av::ui::MessageBox::warning(d, "取消预约", "确认取消这场预约？",
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
          QMessageBox::Yes) {
        return;
      }
      api_.request("/api/v1/conference/meetings/" + m.value("id").toString() + "/cancel", {},
                   [this](QJsonValue, QString e) {
                     message(e);
                     refreshHome();
                   });
      d->accept();
    });
  }
  add_cancel(d, "关闭");
  d->open();
}
}  // namespace meeting
