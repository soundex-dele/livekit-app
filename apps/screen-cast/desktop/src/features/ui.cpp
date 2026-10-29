#include "features/ui.hpp"
#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <map>

namespace screen_cast {
QString normalizedCode(QString text) {
  return text.remove(QRegularExpression("\\s"));
}

bool validCode(const QString& text) {
  return QRegularExpression("^[0-9]{10}$").match(text).hasMatch();
}

bool validServer(const QString& text) {
  QUrl u(text, QUrl::StrictMode);
  return u.isValid() && (u.scheme() == "http" || u.scheme() == "https") && !u.host().isEmpty() &&
         u.userInfo().isEmpty() && !u.hasQuery() && !u.hasFragment();
}

QString readableError(const std::string& error) {
  const auto e = QString::fromStdString(error);
  if (e.isEmpty())
    return {};
  if (e.contains("permission", Qt::CaseInsensitive) || e.contains("权限"))
    return "未获得屏幕录制权限。请在系统设置 → 隐私与安全性 → "
           "屏幕录制中允许本应用，然后重试；系统可能要求重新打开应用。";
  if (e.contains("10-digit"))
    return "请输入 10 位数字投屏码。";
  if (e.contains("code_unavailable"))
    return "投屏码不可用或已过期，请在接收端生成新投屏码。";
  if (e.contains("rate_limited"))
    return "操作过于频繁，请稍后重试。";
  if (e.contains("Display", Qt::CaseInsensitive) || e.contains("display", Qt::CaseInsensitive))
    return "显示器已断开或显示模式发生变化，请刷新并重新选择屏幕。";
  if (e.contains("heartbeat_timeout"))
    return "连接超时，请检查网络并重新生成投屏码。";
  if (e.contains("media_unavailable"))
    return "媒体服务不可用，请检查服务配置后重试。";
  return "投屏未能完成，请检查服务器地址和网络后重试。";
}

namespace {
QLabel* label(QString text, QString role = {}, QWidget* parent = nullptr) {
  auto* w = new QLabel(text, parent);
  w->setWordWrap(true);
  if (!role.isEmpty())
    w->setProperty("role", role);
  return w;
}

QFrame* card() {
  auto* w = new QFrame;
  w->setObjectName("card");
  return w;
}

QPushButton* button(QString text, QString name) {
  auto* b = new QPushButton(text);
  b->setObjectName(name);
  b->setMinimumHeight(40);
  b->setCursor(Qt::PointingHandCursor);
  return b;
}
}  // namespace

Preview::Preview(QWidget* p) : QWidget(p) {
  setMinimumSize(160, 120);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setAccessibleName("共享屏幕预览");
}

void Preview::present(QImage image) {
  image_ = std::move(image);
  update();
}

void Preview::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);
  p.setBrush(image_.isNull() ? QColor("#f0f5ff") : QColor("#101828"));
  p.drawRoundedRect(rect(), 12, 12);
  if (!image_.isNull()) {
    auto fitted = image_.size().scaled(size(), Qt::KeepAspectRatio);
    QRect target(QPoint(0, 0), fitted);
    target.moveCenter(rect().center());
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(target, image_);
    return;
  }
  const QPoint c = rect().center() - QPoint(0, 20);
  QRect monitor(c.x() - 58, c.y() - 38, 116, 70);
  p.setPen(QPen(QColor("#3d73e6"), 3));
  p.setBrush(QColor("#ffffff"));
  p.drawRoundedRect(monitor, 8, 8);
  p.drawLine(c + QPoint(0, 33), c + QPoint(0, 49));
  p.drawLine(c + QPoint(-26, 49), c + QPoint(26, 49));
  p.setPen(QColor("#52627a"));
  p.drawText(QRect(12, c.y() + 67, width() - 24, 44), Qt::AlignCenter | Qt::TextWordWrap,
             "连接成功后，画面将在这里显示");
}

Window::Window(QWidget* p) : QMainWindow(p) {
  setWindowTitle("局域网传屏");
  setMinimumSize(800, 600);
  resize(1040, 720);
  setStyleSheet(R"(
 QWidget{font-family:"PingFang SC","Microsoft YaHei",sans-serif;font-size:14px;color:#172b4d;}
 QMainWindow,QWidget#root{background:#f5f7fb;} QFrame#sidebar,QFrame#card{background:white;border:1px solid #e0e6f0;border-radius:12px;}
 QLabel[role="title"]{font-size:26px;font-weight:600;} QLabel[role="heading"]{font-size:17px;font-weight:600;} QLabel[role="muted"]{color:#52627a;}
 QLabel#status{color:#245ec7;font-weight:600;} QLabel#error{color:#b42318;} QLineEdit{background:white;border:1px solid #a9b7cd;border-radius:8px;padding:10px;selection-background-color:#245ec7;}
 QLineEdit#code{font-size:26px;font-weight:600;padding:14px;} QPushButton{background:white;border:1px solid #b5c2d6;border-radius:8px;padding:8px 16px;}
 QPushButton:hover{background:#edf3ff;border-color:#245ec7;} QPushButton:pressed{background:#dbe8ff;} QPushButton#start,QPushButton#save{background:#245ec7;color:white;border-color:#245ec7;font-weight:600;}
 QPushButton#start:hover,QPushButton#save:hover{background:#194baf;} QPushButton:disabled{background:#e8edf5;color:#697a91;border-color:#d7dfeb;} QPushButton:focus,QLineEdit:focus,QListWidget:focus{border:2px solid #245ec7;}
 QPushButton[nav="true"]{text-align:left;border:0;background:transparent;padding:12px 16px;} QPushButton[nav="true"]:checked{background:#eaf1ff;color:#245ec7;font-weight:600;}
 QListWidget{background:white;border:1px solid #c6d1e2;border-radius:8px;outline:0;} QListWidget::item{padding:10px;border-bottom:1px solid #edf1f7;} QListWidget::item:selected{background:#eaf1ff;color:#164ca9;}
 )");
  auto* root = new QWidget;
  root->setObjectName("root");
  setCentralWidget(root);
  auto* layout = new QHBoxLayout(root);
  layout->setContentsMargins(16, 16, 16, 16);
  layout->setSpacing(20);
  auto* side = new QFrame;
  side->setObjectName("sidebar");
  side->setFixedWidth(156);
  auto* nav = new QVBoxLayout(side);
  nav->setContentsMargins(12, 24, 12, 16);
  nav->setSpacing(12);
  nav->addWidget(label("局域网传屏", "heading"));
  nav->addWidget(label("让分享更简单", "muted"));
  nav->addSpacing(24);
  pages_ = new QStackedWidget;
  pages_->setObjectName("pages");
  for (int i = 0; i < 3; ++i) {
    auto* b = button(QStringList{"投屏", "设置", "帮助"}[i], "nav" + QString::number(i));
    b->setProperty("nav", true);
    b->setCheckable(true);
    b->setAutoExclusive(true);
    b->setParent(side);
    if (i == 0)
      b->setChecked(true);
    nav->addWidget(b);
    connect(b, &QPushButton::clicked, this, [this, i] { pages_->setCurrentIndex(i); });
  }
  nav->addStretch();
  nav->addWidget(label("安全连接\n一次一码", "muted"));
  layout->addWidget(side);
  layout->addWidget(pages_, 1);
  auto* home = new QWidget;
  auto* body = new QVBoxLayout(home);
  body->setContentsMargins(0, 8, 0, 0);
  body->setSpacing(14);
  body->addWidget(label("分享你的屏幕", "title"));
  body->addWidget(label("在接收设备的浏览器中生成投屏码，即可连接。", "muted"));
  status_ = label("准备投屏");
  status_->setObjectName("status");
  status_->setAccessibleName("投屏状态");
  body->addWidget(status_);
  auto* columns = new QHBoxLayout;
  columns->setSpacing(16);
  auto* connection = card();
  connection->setMinimumWidth(278);
  connection->setMaximumWidth(340);
  auto* form = new QVBoxLayout(connection);
  form->setContentsMargins(16, 16, 16, 16);
  form->setSpacing(10);
  auto* codeLabel = label("接收端投屏码", "heading");
  form->addWidget(codeLabel);
  code_ = new QLineEdit;
  code_->setObjectName("code");
  code_->setValidator(
      new QRegularExpressionValidator(QRegularExpression("(?:\\s*[0-9]){10}\\s*"), code_));
  code_->setMaxLength(64);
  code_->setPlaceholderText("输入 10 位投屏码");
  code_->setAccessibleName("10 位投屏码");
  codeLabel->setBuddy(code_);
  form->addWidget(code_);
  form->addWidget(label("投屏码有效期为 5 分钟，仅可使用一次。", "muted"));
  auto* displayLabel = label("选择共享屏幕", "heading");
  auto* displayHeader = new QHBoxLayout;
  displayHeader->addWidget(displayLabel, 1);
  refresh_ = button("刷新", "refresh");
  refresh_->setMinimumHeight(32);
  displayHeader->addWidget(refresh_);
  form->addLayout(displayHeader);
  displays_ = new QListWidget;
  displays_->setObjectName("displays");
  displays_->setAccessibleName("共享显示器");
  displayLabel->setBuddy(displays_);
  displays_->setMinimumHeight(64);
  form->addWidget(displays_, 1);
  start_ = button("开始投屏", "start");
  form->addWidget(start_);
  stop_ = button("停止投屏", "stop");
  form->addWidget(stop_);
  columns->addWidget(connection);
  auto* previewCard = card();
  auto* pv = new QVBoxLayout(previewCard);
  pv->setContentsMargins(16, 16, 16, 16);
  pv->addWidget(label("屏幕预览", "heading"));
  preview_ = new Preview;
  pv->addWidget(preview_, 1);
  pv->addWidget(label("最高 1080p · 15 fps · 仅共享画面", "muted"));
  columns->addWidget(previewCard, 1);
  body->addLayout(columns, 1);
  error_ = label("");
  error_->setObjectName("error");
  error_->setAccessibleName("投屏错误");
  error_->setTextFormat(Qt::PlainText);
  error_->hide();
  body->addWidget(error_);
  body->addWidget(label("最小化后继续共享，关闭窗口将停止投屏。", "muted"));
  pages_->addWidget(home);
  auto* settings = new QWidget;
  auto* sl = new QVBoxLayout(settings);
  sl->setContentsMargins(0, 8, 0, 0);
  sl->setSpacing(20);
  sl->addWidget(label("连接设置", "title"));
  auto* sc = card();
  auto* sf = new QVBoxLayout(sc);
  sf->setContentsMargins(24, 24, 24, 24);
  sf->setSpacing(16);
  auto* serverLabel = label("服务器地址", "heading");
  sf->addWidget(serverLabel);
  server_ = new QLineEdit;
  server_->setObjectName("server");
  server_->setAccessibleName("服务器地址");
  serverLabel->setBuddy(server_);
  server_->setText(QSettings().value("server", "http://127.0.0.1:8080").toString());
  sf->addWidget(server_);
  sf->addWidget(
      label("发送端与接收端需连接同一服务。跨设备使用时，请填写服务所"
            "在电脑的局域网地址。",
            "muted"));
  save_ = button("保存设置", "save");
  sf->addWidget(save_);
  serverHint_ = label("");
  serverHint_->setObjectName("serverHint");
  sf->addWidget(serverHint_);
  sl->addWidget(sc);
  sl->addStretch();
  pages_->addWidget(settings);
  auto* help = new QWidget;
  auto* hl = new QVBoxLayout(help);
  hl->setContentsMargins(0, 8, 0, 0);
  hl->setSpacing(24);
  hl->addWidget(label("开始你的第一次投屏", "title"));
  for (auto text : {"01  在接收设备浏览器打开服务器地址下的 "
                    "/apps/screen-cast/，点击「接收投屏」。",
                    "02  在本应用「设置」中填写相同的服务器地址。",
                    "03  输入接收端显示的 10 位投屏码，选择屏幕，点击「开始投屏」。",
                    "macOS "
                    "首次使用需允许屏幕录制权限。若显示器或分辨率变化，请停止后刷新并重新连"
                    "接。"})
    hl->addWidget(label(text));
  hl->addWidget(label("仅适用于可信局域网 · 不传输声音 · 不提供远程控制", "muted"));
  hl->addStretch();
  pages_->addWidget(help);
  connect(code_, &QLineEdit::textChanged, this, [this](const QString& t) {
    auto n = normalizedCode(t);
    if (n != t) {
      const auto pos = code_->cursorPosition();
      code_->setText(n);
      code_->setCursorPosition(std::min(pos, int(n.size())));
    }
    updateControls();
  });
  connect(server_, &QLineEdit::textChanged, this, [this] {
    serverHint_->clear();
    updateControls();
  });
  connect(displays_, &QListWidget::currentRowChanged, this, [this] { updateControls(); });
  connect(save_, &QPushButton::clicked, this, [this] {
    QSettings().setValue("server", server());
    serverHint_->setText("设置已保存");
  });
  connect(start_, &QPushButton::clicked, this, [this] {
    if (!busy_ && !closing_ && start_->isEnabled()) {
      busy_ = true;
      updateControls();
      showError({});
      QSettings().setValue("server", server());
      emit startRequested();
    }
  });
  connect(code_, &QLineEdit::returnPressed, start_, &QPushButton::click);
  connect(stop_, &QPushButton::clicked, this, [this] {
    stop_->setEnabled(false);
    status_->setText("正在停止投屏…");
    emit stopRequested();
  });
  connect(refresh_, &QPushButton::clicked, this, &Window::refreshRequested);
  resize(QSettings().value("size", QSize(1040, 720)).toSize().expandedTo(minimumSize()));
  updateControls();
}

QString Window::server() const {
  return server_->text().trimmed();
}

QString Window::code() const {
  return code_->text();
}

livekit::sdk::ScreenCaptureDevice Window::selectedDevice() const {
  int i = displays_->currentRow();
  return i >= 0 && i < int(devices_.size()) ? devices_[i] : livekit::sdk::ScreenCaptureDevice{};
}

void Window::setDevices(std::vector<livekit::sdk::ScreenCaptureDevice> devices) {
  auto id = selectedDevice().id;
  devices_ = std::move(devices);
  displays_->clear();
  int selected = 0;
  for (int i = 0; i < int(devices_.size()); ++i) {
    const auto& d = devices_[i];
    auto* item =
        new QListWidgetItem(QString::fromStdString(d.name) + (d.primary ? " · 主屏" : "") + "\n" +
                                QString::number(d.width) + " × " + QString::number(d.height),
                            displays_);
    item->setToolTip(QString::fromStdString(d.id));
    if (d.id == id || (id.empty() && d.primary))
      selected = i;
  }
  if (!devices_.empty())
    displays_->setCurrentRow(selected);
  else
    showError("未发现可用显示器，请连接显示器后刷新。");
  updateControls();
}

void Window::setEnumerating(bool busy) {
  enumerating_ = busy;
  refresh_->setText(busy ? "刷新中…" : "刷新");
  updateControls();
}

void Window::showError(const QString& message) {
  error_->setText(message);
  error_->setVisible(!message.isEmpty());
}

void Window::setPreview(QImage image) {
  preview_->present(std::move(image));
}

void Window::present(const Snapshot& s) {
  busy_ = !s.finished;
  const std::map<std::string, QString> labels{
      {"idle", "准备投屏"},        {"permission", "正在检查屏幕录制权限…"},
      {"connecting", "正在连接…"}, {"paired", "已连接，等待接收端…"},
      {"streaming", "正在投屏"},   {"stopping", "正在停止投屏…"},
      {"ended", "投屏已结束"}};
  auto it = labels.find(s.state);
  status_->setText(!s.error.empty()     ? "投屏遇到问题"
                   : it == labels.end() ? "等待连接"
                                        : it->second);
  showError(readableError(s.error));
  if (s.finished || s.state == "stopping")
    setPreview({});
  updateControls();
  if (s.state == "stopping")
    stop_->setEnabled(false);
}

void Window::updateControls() {
  const bool editable = !busy_ && !closing_;
  start_->setVisible(!busy_);
  stop_->setVisible(busy_);
  code_->setEnabled(editable);
  server_->setEnabled(editable);
  displays_->setEnabled(editable && !enumerating_);
  refresh_->setEnabled(editable && !enumerating_);
  save_->setEnabled(editable && validServer(server()));
  start_->setEnabled(editable && !enumerating_ && validCode(code()) && validServer(server()) &&
                     displays_->currentRow() >= 0);
  stop_->setEnabled(busy_ && !closing_);
  if (!validServer(server()))
    serverHint_->setText("请输入有效的 HTTP 或 HTTPS 服务器地址，不含账号、查询参数或片段。");
  else if (serverHint_->text() != "设置已保存")
    serverHint_->clear();
}

void Window::closeEvent(QCloseEvent* e) {
  if (allowClose_) {
    QSettings().setValue("size", size());
    e->accept();
    return;
  }
  e->ignore();
  if (!closing_) {
    closing_ = true;
    updateControls();
    status_->setText("正在释放投屏资源…");
    emit closeRequested();
  }
}

void Window::finishClose() {
  allowClose_ = true;
  close();
}
}  // namespace screen_cast
