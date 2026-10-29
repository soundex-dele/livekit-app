#include "window.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace studio_app {
namespace {
QString q(const std::string& value) {
  return QString::fromUtf8(value.data(), qsizetype(value.size()));
}

std::string s(const QString& value) {
  return value.toUtf8().toStdString();
}

std::filesystem::path path(const QString& value) {
  return std::filesystem::u8path(s(value));
}

std::string id() {
  return s(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

QLabel* label(const QString& text) {
  auto* l = new QLabel(text);
  l->setTextFormat(Qt::PlainText);
  l->setWordWrap(true);
  return l;
}

QPushButton* button(const QString& text, const QString& name, QBoxLayout* layout, QObject* context,
                    std::function<void()> action) {
  auto* b = new QPushButton(text);
  b->setObjectName(name);
  b->setMinimumHeight(32);
  layout->addWidget(b);
  QObject::connect(b, &QPushButton::clicked, context, [action = std::move(action)] { action(); });
  return b;
}

QScrollArea* scrollPanel(QWidget* widget) {
  auto* area = new QScrollArea;
  area->setWidgetResizable(true);
  area->setFrameShape(QFrame::NoFrame);
  area->setWidget(widget);
  return area;
}
}  // namespace

void configureOffscreenPlatform() {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("QT_FORCE_STDERR_LOGGING", "1");
#ifdef _WIN32
  // The offscreen plugin uses FreeType rather than the Windows font database.
  // Read installed fonts in place; never redistribute system font files.
  if (qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR"))
    qputenv("QT_QPA_FONTDIR", (qEnvironmentVariable("WINDIR") + "/Fonts").toUtf8());
#endif
}

void applyTheme() {
  QApplication::setStyle("Fusion");
  QPalette palette;
  palette.setColor(QPalette::Window, QColor("#101620"));
  palette.setColor(QPalette::WindowText, QColor("#eef2fa"));
  palette.setColor(QPalette::Base, QColor("#0d121c"));
  palette.setColor(QPalette::AlternateBase, QColor("#1b2535"));
  palette.setColor(QPalette::Text, QColor("#eef2fa"));
  palette.setColor(QPalette::PlaceholderText, QColor("#9aa9c0"));
  palette.setColor(QPalette::Button, QColor("#253249"));
  palette.setColor(QPalette::ButtonText, QColor("#eef2fa"));
  palette.setColor(QPalette::Highlight, QColor("#9d3156"));
  palette.setColor(QPalette::HighlightedText, Qt::white);
  palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#8895ab"));
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#8895ab"));
  qApp->setPalette(palette);
  qApp->setStyleSheet(QStringLiteral(R"(
    QWidget { font-size: 13px; }
    QGroupBox { border: 1px solid #354158; border-radius: 8px; margin-top: 14px; padding: 12px 8px 8px; }
    QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 5px; color: #b8c2d6; }
    QPushButton { padding: 5px 10px; border: 1px solid #40506a; border-radius: 5px; background: #253249; }
    QPushButton:hover { background: #344761; }
    QPushButton:pressed { background: #172333; }
    QPushButton:disabled { background: #1a2230; color: #8895ab; }
    QPushButton#startLive { background: #ba355c; border-color: #dc527b; color: white; font-weight: bold; }
    QPushButton#startLive:disabled { background: #543044; color: #bca4af; }
    QLineEdit, QPlainTextEdit, QListWidget, QComboBox, QDoubleSpinBox { padding: 5px; border: 1px solid #40506a; border-radius: 4px; }
    QPushButton:focus, QLineEdit:focus, QPlainTextEdit:focus, QListWidget:focus, QComboBox:focus, QDoubleSpinBox:focus { border: 2px solid #ff8caa; }
    QListWidget::item { padding: 7px 4px; }
    QListWidget::item:selected { background: #543044; }
    QTabWidget::pane { border: 1px solid #354158; }
    QTabBar::tab { padding: 9px 12px; }
    QProgressBar { border: 1px solid #40506a; border-radius: 3px; max-height: 12px; background: #0d121c; }
    QProgressBar::chunk { background: #58ccab; }
    QLabel#error { color: #ff9bac; }
    QLabel#brand { color: #ff8caa; font-size: 18px; font-weight: bold; }
  )"));
}

struct Window::Impl {
  Window* w;
  WindowOptions config;
  std::filesystem::path project;
  sdk::StudioOptions options;
  sdk::StudioAudioSettings audio;
  std::vector<sdk::StudioScene> scenes;
  size_t selected = 0;
  int source = -1;
  std::unique_ptr<Session> session;
  bool dirty = false;
  QString error;
  QTimer timer, saveTimer;
  QElapsedTimer fpsClock;
  uint64_t lastFrames = 0;
  double fps = 0;
  std::shared_ptr<const sdk::StudioImage> lastFrame;
  QListWidget *sceneList = nullptr, *sourceList = nullptr;
  QLineEdit *sceneName = nullptr, *title = nullptr, *message = nullptr;
  QPushButton *liveButton = nullptr, *recordButton = nullptr, *accountButton = nullptr,
              *shareButton = nullptr;
  QComboBox* format = nullptr;
  Preview* preview = nullptr;
  QScrollArea* properties = nullptr;
  QPlainTextEdit* messages = nullptr;
  QTabWidget* interactions = nullptr;
  QWidget *guestPage = nullptr, *pkPage = nullptr;
  QLabel *status = nullptr, *errorLabel = nullptr, *online = nullptr;
  QProgressBar *micPeak = nullptr, *systemPeak = nullptr;
  Json oldEvents, oldLive, oldRooms;
  std::string oldUser;

  Impl(Window* owner, WindowOptions cfg)
      : w(owner), config(std::move(cfg)), project(config.settings / "project.json") {
    if (std::filesystem::exists(project)) {
      try {
        scenes = sdk::Studio::load_project(project, selected, options, audio);
      } catch (const std::exception& e) {
        error = q(e.what());
      }
    }
    if (config.portrait) {
      options.width = 720;
      options.height = 1280;
    }
    if (scenes.empty()) {
      sdk::StudioScene scene;
      sdk::StudioSource background;
      background.id = "background";
      background.name = "背景";
      sdk::StudioSource text;
      text.id = "welcome";
      text.name = "标题";
      text.kind = sdk::StudioSourceKind::text;
      text.input = "与你一起，分享此刻。";
      text.color = {.95f, .96f, 1, 1};
      text.transform = {.08f, .39f, .82f, options.width < options.height ? .07f : .22f};
      scene.sources = {background, text};
      scenes.push_back(scene);
      selected = 0;
    }
    selected = std::min(selected, scenes.size() - 1);
    auto runtime = options;
    runtime.software_gpu = config.offline;
    session = std::make_unique<Session>(config.settings / "account.bin", runtime, config.offline);
    build();
    refreshScenes();
    refreshSources();
    apply();
    fpsClock.start();
    QObject::connect(&timer, &QTimer::timeout, w, [this] { guarded([this] { tick(); }); });
    timer.start(33);
    saveTimer.setSingleShot(true);
    saveTimer.setInterval(700);
    QObject::connect(&saveTimer, &QTimer::timeout, w, [this] { save(); });
    tick();
  }

  ~Impl() {
    timer.stop();
    saveTimer.stop();
  }

  sdk::StudioScene& scene() {
    return scenes[selected];
  }

  sdk::StudioSource* item() {
    return source >= 0 && source < int(scene().sources.size()) ? &scene().sources[source] : nullptr;
  }

  void guarded(const std::function<void()>& action) {
    try {
      action();
    } catch (const std::exception& e) {
      error = q(e.what());
      errorLabel->setText(error);
    }
  }

  void apply() {
    auto runtime = scene();
    auto sound = audio;
    if (config.offline) {
      std::erase_if(runtime.sources, [](const auto& i) {
        return i.kind != sdk::StudioSourceKind::color && i.kind != sdk::StudioSourceKind::text &&
               i.kind != sdk::StudioSourceKind::image;
      });
      sound = {};
    }
    session->set_scene(std::move(runtime));
    session->set_audio(sound);
    preview->setScene(scene(), source);
  }

  void changed() {
    apply();
    dirty = true;
    saveTimer.start();
    if (item() && sourceList->currentItem())
      sourceList->currentItem()->setText((item()->visible ? QString() : QStringLiteral("[隐藏] ")) +
                                         q(item()->name));
  }

  bool save() {
    if (!dirty)
      return true;
    try {
      sdk::Studio::save_project(project, scenes, selected, options, audio);
      dirty = false;
      return true;
    } catch (const std::exception& e) {
      error = q(e.what());
      errorLabel->setText(error);
      return false;
    }
  }

  QPushButton* action(const QString& text, const QString& name, QBoxLayout* layout,
                      std::function<void()> fn) {
    return button(text, name, layout, w, [this, fn = std::move(fn)] { guarded(fn); });
  }

  void build() {
    w->setWindowTitle(QStringLiteral("AV 直播助手 · Qt 6"));
    w->resize(1480, 920);
    w->setMinimumSize(1080, 700);
    auto* root = new QWidget;
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(16, 16, 16, 12);
    layout->setSpacing(12);
    w->setCentralWidget(root);
    auto* top = new QHBoxLayout;
    top->setSpacing(12);
    layout->addLayout(top);
    auto* brand = label(QStringLiteral("AV  LIVE STUDIO"));
    brand->setObjectName("brand");
    top->addWidget(brand);
    top->addWidget(label(QStringLiteral("直播标题")));
    title = new QLineEdit(QStringLiteral("分享此刻的精彩"));
    title->setObjectName("liveTitle");
    title->setAccessibleName(QStringLiteral("直播标题"));
    top->addWidget(title, 1);
    accountButton = action(QStringLiteral("登录 / 注册"), "account", top, [this] { account(); });
    recordButton = action(QStringLiteral("本地录制"), "record", top, [this] { record(); });
    liveButton = action(QStringLiteral("开始直播"), "startLive", top, [this] {
      auto state = session->snapshot();
      if (state.broadcast.empty())
        session->start(s(title->text()));
      else if (QMessageBox::question(w, QStringLiteral("结束直播"),
                                     QStringLiteral("结束本次直播并断开连麦？本地录制可继续。")) ==
               QMessageBox::Yes)
        session->stop();
    });
    auto* split = new QSplitter;
    split->setChildrenCollapsible(false);
    layout->addWidget(split, 1);
    auto* left = new QWidget;
    auto* ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 8, 0);
    auto* scenesBox = new QGroupBox(QStringLiteral("场景"));
    auto* sl = new QVBoxLayout(scenesBox);
    ll->addWidget(scenesBox);
    sceneList = new QListWidget;
    sceneList->setObjectName("scenes");
    sceneList->setAccessibleName(QStringLiteral("场景列表"));
    sceneList->setMaximumHeight(125);
    sl->addWidget(sceneList);
    auto* sceneActions = new QHBoxLayout;
    sl->addLayout(sceneActions);
    action(QStringLiteral("新建"), "addScene", sceneActions, [this] {
      if (scenes.size() >= 20)
        return;
      sdk::StudioScene entry;
      entry.id = id();
      entry.name = "场景 " + std::to_string(scenes.size() + 1);
      scenes.push_back(entry);
      selected = scenes.size() - 1;
      source = -1;
      refreshScenes();
      refreshSources();
      changed();
    });
    action(QStringLiteral("删除"), "deleteScene", sceneActions, [this] {
      if (scenes.size() < 2)
        return;
      scenes.erase(scenes.begin() + selected);
      selected = 0;
      source = -1;
      refreshScenes();
      refreshSources();
      changed();
    });
    sceneName = new QLineEdit;
    sceneName->setObjectName("sceneName");
    sceneName->setAccessibleName(QStringLiteral("场景名称"));
    sl->addWidget(sceneName);
    QObject::connect(sceneName, &QLineEdit::textEdited, w, [this](const QString& text) {
      scene().name = s(text);
      sceneList->currentItem()->setText(text);
      changed();
    });
    QObject::connect(sceneList, &QListWidget::currentRowChanged, w, [this](int row) {
      if (row < 0)
        return;
      selected = size_t(row);
      source = -1;
      sceneName->setText(q(scene().name));
      refreshSources();
      changed();
    });
    auto* sourcesBox = new QGroupBox(QStringLiteral("素材 · 上层优先"));
    auto* sourcesLayout = new QVBoxLayout(sourcesBox);
    ll->addWidget(sourcesBox);
    action(QStringLiteral("+ 添加素材"), "addSource", sourcesLayout, [this] { addSource(); });
    sourceList = new QListWidget;
    sourceList->setObjectName("sources");
    sourceList->setAccessibleName(QStringLiteral("素材列表"));
    sourceList->setMaximumHeight(150);
    sourcesLayout->addWidget(sourceList);
    auto* sourceActions = new QHBoxLayout;
    sourcesLayout->addLayout(sourceActions);
    action(QStringLiteral("上移"), "sourceUp", sourceActions, [this] {
      if (item() && source + 1 < int(scene().sources.size())) {
        std::swap(scene().sources[source], scene().sources[source + 1]);
        ++source;
        refreshSources();
        changed();
      }
    });
    action(QStringLiteral("下移"), "sourceDown", sourceActions, [this] {
      if (item() && source > 0) {
        std::swap(scene().sources[source], scene().sources[source - 1]);
        --source;
        refreshSources();
        changed();
      }
    });
    action(QStringLiteral("删除"), "deleteSource", sourceActions, [this] {
      if (item()) {
        scene().sources.erase(scene().sources.begin() + source);
        source = -1;
        refreshSources();
        changed();
      }
    });
    QObject::connect(sourceList, &QListWidget::currentRowChanged, w, [this](int row) {
      source = row < 0 ? -1 : int(scene().sources.size()) - 1 - row;
      refreshProperties();
      updateSourceActions();
      preview->setScene(scene(), source);
    });
    properties = new QScrollArea;
    properties->setObjectName("properties");
    properties->setWidgetResizable(true);
    properties->setFrameShape(QFrame::NoFrame);
    ll->addWidget(properties, 1);
    split->addWidget(left);
    auto* center = new QWidget;
    auto* cl = new QVBoxLayout(center);
    cl->setContentsMargins(8, 0, 8, 0);
    auto* previewHeader = new QHBoxLayout;
    previewHeader->addWidget(label(QStringLiteral("节目预览")));
    previewHeader->addStretch();
    previewHeader->addWidget(label(QStringLiteral("PROGRAM")));
    cl->addLayout(previewHeader);
    preview = new Preview;
    cl->addWidget(preview, 1);
    preview->selectSource = [this](int index) {
      source = index;
      refreshSources();
    };
    preview->moveSource = [this](float dx, float dy, bool resize) {
      if (!item() || item()->locked)
        return;
      auto& t = item()->transform;
      if (resize) {
        t.width = std::clamp(t.width + dx, .01f, 4.f);
        t.height = std::clamp(t.height + dy, .01f, 4.f);
      } else {
        t.x = std::clamp(t.x + dx, -2.f, 2.f);
        t.y = std::clamp(t.y + dy, -2.f, 2.f);
      }
      syncTransform();
      changed();
    };
    auto* hint = label(QStringLiteral("拖动 / 方向键移动 · Shift 调整尺寸 · 左侧属性精确编辑"));
    hint->setStyleSheet("color: #b8c2d6;");
    cl->addWidget(hint);
    auto* mixer = new QGroupBox(QStringLiteral("混音器"));
    auto* mix = new QVBoxLayout(mixer);
    cl->addWidget(mixer);
    auto audioRow = [this, mix](const QString& text, const QString& name, bool* enabled,
                                float* volume, QProgressBar** meter) {
      auto* row = new QHBoxLayout;
      mix->addLayout(row);
      auto* check = new QCheckBox(text);
      check->setObjectName(name);
      check->setChecked(*enabled);
      check->setEnabled(!config.offline);
      row->addWidget(check);
      auto* gain = new QDoubleSpinBox;
      gain->setRange(0, 2);
      gain->setSingleStep(.05);
      gain->setValue(*volume);
      gain->setAccessibleName(text + QStringLiteral("音量"));
      row->addWidget(gain);
      if (meter) {
        *meter = new QProgressBar;
        (*meter)->setRange(0, 100);
        (*meter)->setValue(0);
        (*meter)->setTextVisible(false);
        (*meter)->setAccessibleName(text + QStringLiteral("电平"));
        row->addWidget(*meter, 1);
      } else
        row->addStretch();
      QObject::connect(check, &QCheckBox::toggled, w, [this, enabled](bool value) {
        *enabled = value;
        changed();
      });
      QObject::connect(gain, &QDoubleSpinBox::valueChanged, w, [this, volume](double value) {
        *volume = float(value);
        changed();
      });
    };
    audioRow(QStringLiteral("麦克风"), "microphone", &audio.microphone, &audio.microphone_volume,
             &micPeak);
    audioRow(QStringLiteral("系统声音"), "systemAudio", &audio.system, &audio.system_volume,
             &systemPeak);
    audioRow(QStringLiteral("监听连麦"), "monitor", &audio.monitor, &audio.remote_volume, nullptr);
    auto* fmtRow = new QHBoxLayout;
    cl->addLayout(fmtRow);
    fmtRow->addWidget(label(QStringLiteral("画布 · 30 fps")));
    format = new QComboBox;
    format->setObjectName("format");
    format->setAccessibleName(QStringLiteral("输出画布尺寸"));
    format->addItems({QStringLiteral("横屏 1280 × 720"), QStringLiteral("竖屏 720 × 1280"),
                      QStringLiteral("横屏 1920 × 1080"), QStringLiteral("竖屏 1080 × 1920")});
    format->setCurrentIndex(options.width == 1280   ? 0
                            : options.width == 720  ? 1
                            : options.width == 1920 ? 2
                                                    : 3);
    fmtRow->addWidget(format, 1);
    QObject::connect(format, &QComboBox::activated, w, [this](int index) {
      guarded([this, index] {
        auto previous = options;
        options.width = std::array{1280, 720, 1920, 1080}[index];
        options.height = std::array{720, 1280, 1080, 1920}[index];
        float ratio =
            (float(options.width) / options.height) / (float(previous.width) / previous.height);
        for (auto& entry : scenes)
          for (auto& src : entry.sources)
            if (src.kind == sdk::StudioSourceKind::text)
              src.transform.height = std::clamp(src.transform.height * ratio, .01f, 4.f);
        changed();
        auto runtime = options;
        runtime.software_gpu = config.offline;
        session->reconfigure(runtime);
        syncTransform();
      });
    });
    split->addWidget(center);
    auto* right = new QWidget;
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(8, 0, 0, 0);
    auto* liveHeader = new QHBoxLayout;
    rl->addLayout(liveHeader);
    online = label(QStringLiteral("直播互动 · 未开播"));
    liveHeader->addWidget(online, 1);
    shareButton = action(QStringLiteral("观看地址"), "share", liveHeader, [this] {
      auto state = session->snapshot();
      if (state.live.is_object() && state.live.contains("broadcast"))
        QApplication::clipboard()->setText(q(state.server_url + "/apps/live-studio/#room/" +
                                             state.live.at("broadcast").value("room", "")));
    });
    interactions = new QTabWidget;
    rl->addWidget(interactions, 1);
    auto* chat = new QWidget;
    auto* chatLayout = new QVBoxLayout(chat);
    messages = new QPlainTextEdit;
    messages->setObjectName("messages");
    messages->setReadOnly(true);
    messages->setPlaceholderText(QStringLiteral("开播后，聊天消息与礼物会显示在这里。"));
    chatLayout->addWidget(messages, 1);
    message = new QLineEdit;
    message->setObjectName("message");
    message->setAccessibleName(QStringLiteral("聊天消息"));
    message->setPlaceholderText(QStringLiteral("输入消息…"));
    chatLayout->addWidget(message);
    auto send = [this] {
      if (!message->text().trimmed().isEmpty()) {
        session->command("chat", {{"text", s(message->text())}});
        message->clear();
      }
    };
    action(QStringLiteral("发送"), "sendMessage", chatLayout, send);
    QObject::connect(message, &QLineEdit::returnPressed, w, send);
    interactions->addTab(chat, QStringLiteral("聊天"));
    guestPage = new QWidget;
    pkPage = new QWidget;
    interactions->addTab(scrollPanel(guestPage), QStringLiteral("连麦"));
    interactions->addTab(scrollPanel(pkPage), QStringLiteral("PK / 榜单"));
    split->addWidget(right);
    split->setSizes({280, 800, 320});
    split->setStretchFactor(1, 1);
    errorLabel = label(error);
    errorLabel->setObjectName("error");
    layout->addWidget(errorLabel);
    status = label("");
    w->statusBar()->addWidget(status, 1);
  }

  void refreshScenes() {
    QSignalBlocker block(sceneList);
    sceneList->clear();
    for (const auto& entry : scenes)
      sceneList->addItem(q(entry.name));
    sceneList->setCurrentRow(int(selected));
    sceneName->setText(q(scene().name));
    w->findChild<QPushButton*>("deleteScene")->setEnabled(scenes.size() > 1);
    w->findChild<QPushButton*>("addScene")->setEnabled(scenes.size() < 20);
  }

  void refreshSources() {
    QSignalBlocker block(sourceList);
    sourceList->clear();
    for (auto it = scene().sources.rbegin(); it != scene().sources.rend(); ++it)
      sourceList->addItem((it->visible ? QString() : QStringLiteral("[隐藏] ")) + q(it->name));
    sourceList->setCurrentRow(source < 0 ? -1 : int(scene().sources.size()) - 1 - source);
    refreshProperties();
    preview->setScene(scene(), source);
    updateSourceActions();
  }

  void updateSourceActions() {
    w->findChild<QPushButton*>("deleteSource")->setEnabled(item());
    w->findChild<QPushButton*>("sourceUp")
        ->setEnabled(item() && source + 1 < int(scene().sources.size()));
    w->findChild<QPushButton*>("sourceDown")->setEnabled(source > 0);
  }

  void syncTransform() {
    if (!item())
      return;
    for (auto field :
         std::array<std::pair<const char*, float>, 4>{{{"x", item()->transform.x},
                                                       {"y", item()->transform.y},
                                                       {"width", item()->transform.width},
                                                       {"height", item()->transform.height}}}) {
      if (auto* spin = properties->findChild<QDoubleSpinBox*>(field.first)) {
        QSignalBlocker block(spin);
        spin->setValue(field.second);
      }
    }
  }

  void refreshProperties() {
    auto* pane = new QWidget;
    auto* form = new QFormLayout(pane);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    properties->setWidget(pane);
    if (!item()) {
      form->addRow(label(QStringLiteral("选择素材以编辑属性")));
      return;
    }
    auto* name = new QLineEdit(q(item()->name));
    name->setObjectName("sourceName");
    form->addRow(QStringLiteral("名称"), name);
    QObject::connect(name, &QLineEdit::textEdited, w, [this](const QString& v) {
      if (!item())
        return;
      item()->name = s(v);
      if (auto* row = sourceList->currentItem())
        row->setText(q(item()->name));
      changed();
    });
    auto check = [this, form](const QString& text, const QString& key, bool value,
                              std::function<void(sdk::StudioSource&, bool)> fn) {
      auto* c = new QCheckBox(text);
      c->setObjectName(key);
      c->setChecked(value);
      form->addRow(c);
      QObject::connect(c, &QCheckBox::toggled, w, [this, fn](bool v) {
        if (item()) {
          fn(*item(), v);
          changed();
        }
      });
      return c;
    };
    check(QStringLiteral("显示素材"), "visible", item()->visible,
          [](auto& i, bool v) { i.visible = v; });
    auto* locked = check(QStringLiteral("锁定变换"), "locked", item()->locked,
                         [](auto& i, bool v) { i.locked = v; });
    auto* transformPane = new QWidget;
    auto* transforms = new QFormLayout(transformPane);
    transforms->setContentsMargins(0, 0, 0, 0);
    form->addRow(transformPane);
    transformPane->setEnabled(!item()->locked);
    QObject::connect(locked, &QCheckBox::toggled, transformPane,
                     [transformPane](bool value) { transformPane->setEnabled(!value); });
    auto number = [this](QFormLayout* target, const QString& text, const QString& key, float value,
                         double min, double max,
                         std::function<void(sdk::StudioSource&, float)> fn) {
      auto* spin = new QDoubleSpinBox;
      spin->setObjectName(key);
      spin->setDecimals(3);
      spin->setRange(min, max);
      spin->setSingleStep(.01);
      spin->setValue(value);
      target->addRow(text, spin);
      QObject::connect(spin, &QDoubleSpinBox::valueChanged, w, [this, fn](double value) {
        if (item()) {
          fn(*item(), float(value));
          changed();
        }
      });
    };
    auto t = item()->transform;
    number(transforms, QStringLiteral("位置 X"), "x", t.x, -2, 2,
           [](auto& i, float v) { i.transform.x = v; });
    number(transforms, QStringLiteral("位置 Y"), "y", t.y, -2, 2,
           [](auto& i, float v) { i.transform.y = v; });
    number(transforms, QStringLiteral("宽度"), "width", t.width, .01, 4,
           [](auto& i, float v) { i.transform.width = v; });
    number(transforms, QStringLiteral("高度"), "height", t.height, .01, 4,
           [](auto& i, float v) { i.transform.height = v; });
    number(transforms, QStringLiteral("旋转"), "rotation", t.rotation, -180, 180,
           [](auto& i, float v) { i.transform.rotation = v; });
    number(transforms, QStringLiteral("左裁剪"), "cropLeft", t.crop_left, 0, .45,
           [](auto& i, float v) { i.transform.crop_left = v; });
    number(transforms, QStringLiteral("右裁剪"), "cropRight", t.crop_right, 0, .45,
           [](auto& i, float v) { i.transform.crop_right = v; });
    number(transforms, QStringLiteral("上裁剪"), "cropTop", t.crop_top, 0, .45,
           [](auto& i, float v) { i.transform.crop_top = v; });
    number(transforms, QStringLiteral("下裁剪"), "cropBottom", t.crop_bottom, 0, .45,
           [](auto& i, float v) { i.transform.crop_bottom = v; });
    auto* fill = new QPushButton(QStringLiteral("铺满画布"));
    fill->setObjectName("fillCanvas");
    transforms->addRow(fill);
    QObject::connect(fill, &QPushButton::clicked, w, [this] {
      if (item()) {
        item()->transform = {};
        changed();
        QTimer::singleShot(0, w, [this] { refreshProperties(); });
      }
    });
    if (item()->kind == sdk::StudioSourceKind::text) {
      auto* text = new QPlainTextEdit(q(item()->input));
      text->setObjectName("sourceText");
      text->setMaximumHeight(85);
      form->addRow(QStringLiteral("内容"), text);
      QObject::connect(text, &QPlainTextEdit::textChanged, w, [this, text] {
        if (item()) {
          item()->input = s(text->toPlainText());
          changed();
        }
      });
    }
    auto color = [this, form](const QString& text, bool chroma) {
      auto* b = new QPushButton(text);
      form->addRow(b);
      QObject::connect(b, &QPushButton::clicked, w, [this, chroma] {
        if (!item())
          return;
        auto sourceId = item()->id;
        auto c = chroma ? QColor::fromRgbF(item()->chroma.color[0], item()->chroma.color[1],
                                           item()->chroma.color[2])
                        : QColor::fromRgbF(item()->color[0], item()->color[1], item()->color[2],
                                           item()->color[3]);
        auto selectedColor = QColorDialog::getColor(
            c, w, QStringLiteral("选择颜色"),
            chroma ? QColorDialog::ColorDialogOptions{} : QColorDialog::ShowAlphaChannel);
        if (!selectedColor.isValid() || !item() || item()->id != sourceId)
          return;
        if (chroma)
          item()->chroma.color = {float(selectedColor.redF()), float(selectedColor.greenF()),
                                  float(selectedColor.blueF())};
        else
          item()->color = {float(selectedColor.redF()), float(selectedColor.greenF()),
                           float(selectedColor.blueF()), float(selectedColor.alphaF())};
        changed();
      });
    };
    if (item()->kind == sdk::StudioSourceKind::text || item()->kind == sdk::StudioSourceKind::color)
      color(QStringLiteral("素材颜色 / 透明度"), false);
    check(QStringLiteral("绿幕抠像"), "chroma", item()->chroma.enabled,
          [](auto& i, bool v) { i.chroma.enabled = v; });
    color(QStringLiteral("抠像背景色"), true);
    number(form, QStringLiteral("相似度"), "similarity", item()->chroma.similarity, 0, 1,
           [](auto& i, float v) { i.chroma.similarity = v; });
    number(form, QStringLiteral("边缘平滑"), "smoothness", item()->chroma.smoothness, .01, .5,
           [](auto& i, float v) { i.chroma.smoothness = v; });
    number(form, QStringLiteral("抑制溢色"), "spill", item()->chroma.spill, 0, 1,
           [](auto& i, float v) { i.chroma.spill = v; });
    if (item()->kind == sdk::StudioSourceKind::video) {
      check(QStringLiteral("循环播放"), "loop", item()->loop, [](auto& i, bool v) { i.loop = v; });
      check(QStringLiteral("素材静音"), "muted", item()->muted,
            [](auto& i, bool v) { i.muted = v; });
      number(form, QStringLiteral("素材音量"), "volume", item()->volume, 0, 2,
             [](auto& i, float v) { i.volume = v; });
    }
  }

  void addSource() {
    QDialog dialog(w);
    dialog.setWindowTitle(QStringLiteral("添加素材"));
    dialog.setMinimumWidth(480);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    auto* kind = new QComboBox;
    kind->setObjectName("sourceKind");
    kind->addItems({QStringLiteral("屏幕捕获"), QStringLiteral("窗口捕获"),
                    QStringLiteral("摄像头"), QStringLiteral("图片"), QStringLiteral("文字"),
                    QStringLiteral("本地视频"), QStringLiteral("纯色背景")});
    kind->setCurrentIndex(4);
    form->addRow(QStringLiteral("素材类型"), kind);
    auto* input = new QPlainTextEdit;
    input->setObjectName("sourceInput");
    input->setMaximumHeight(100);
    form->addRow(QStringLiteral("文字 / 文件路径"), input);
    auto* devices = new QComboBox;
    devices->setObjectName("devices");
    form->addRow(QStringLiteral("设备"), devices);
    auto* row = new QHBoxLayout;
    layout->addLayout(row);
    auto* browse = new QPushButton(QStringLiteral("选择文件…"));
    auto* refresh = new QPushButton(QStringLiteral("刷新设备"));
    row->addWidget(browse);
    row->addWidget(refresh);
    auto* info = label("");
    layout->addWidget(info);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("添加"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName("confirmSource");
    layout->addWidget(buttons);
    using DeviceResult = std::pair<std::vector<sdk::StudioDevice>, QString>;
    auto* watcher = new QFutureWatcher<DeviceResult>(&dialog);
    int requestKind = -1;
    auto validate = [&] {
      int k = kind->currentIndex();
      buttons->button(QDialogButtonBox::Ok)
          ->setEnabled(k < 3 ? (!config.offline && devices->count() > 0 && !watcher->isRunning())
                             : k == 6 || !input->toPlainText().trimmed().isEmpty());
    };
    auto enumerate = [&] {
      if (config.offline || watcher->isRunning() || kind->currentIndex() > 2)
        return;
      requestKind = kind->currentIndex();
      devices->clear();
      info->setText(QStringLiteral("正在获取设备列表，请按系统提示授权…"));
      refresh->setEnabled(false);
      watcher->setFuture(QtConcurrent::run([k = requestKind]() -> DeviceResult {
        try {
          return {k == 0   ? sdk::studio_screens()
                  : k == 1 ? sdk::studio_windows()
                           : sdk::studio_cameras(),
                  {}};
        } catch (const std::exception& e) {
          return {{}, q(e.what())};
        }
      }));
      validate();
    };
    QObject::connect(watcher, &QFutureWatcher<DeviceResult>::finished, &dialog, [&] {
      if (requestKind == kind->currentIndex()) {
        auto result = watcher->result();
        devices->clear();
        for (const auto& device : result.first)
          devices->addItem(q(device.name), q(device.id));
        info->setText(result.second.isEmpty() && devices->count() == 0
                          ? QStringLiteral("未发现设备，请检查连接或权限后刷新。")
                          : result.second);
      }
      refresh->setEnabled(!config.offline);
      validate();
    });
    auto update = [&] {
      int k = kind->currentIndex();
      devices->setVisible(k < 3);
      refresh->setVisible(k < 3);
      refresh->setEnabled(!config.offline && !watcher->isRunning());
      input->setVisible(k >= 3 && k != 6);
      browse->setVisible(k == 3 || k == 5);
      devices->clear();
      info->setText(config.offline && k < 3 ? QStringLiteral("离屏模式不访问采集设备") : QString());
      validate();
    };
    QObject::connect(kind, &QComboBox::currentIndexChanged, &dialog, [&] {
      update();
      enumerate();
    });
    QObject::connect(input, &QPlainTextEdit::textChanged, &dialog, validate);
    QObject::connect(devices, &QComboBox::currentIndexChanged, &dialog, validate);
    QObject::connect(refresh, &QPushButton::clicked, &dialog, enumerate);
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
      auto file = QFileDialog::getOpenFileName(&dialog, QStringLiteral("选择素材"));
      if (!file.isEmpty())
        input->setPlainText(file);
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    update();
    if (dialog.exec() != QDialog::Accepted)
      return;
    sdk::StudioSource entry;
    entry.id = id();
    entry.name = s(kind->currentText());
    int k = kind->currentIndex();
    entry.kind = std::array{sdk::StudioSourceKind::screen, sdk::StudioSourceKind::window,
                            sdk::StudioSourceKind::camera, sdk::StudioSourceKind::image,
                            sdk::StudioSourceKind::text,   sdk::StudioSourceKind::video,
                            sdk::StudioSourceKind::color}[k];
    entry.input = s(k < 3 ? devices->currentData().toString() : input->toPlainText());
    if (k == 4) {
      entry.transform = {.08f, .76f, .8f, .18f};
      entry.color = {1, 1, 1, 1};
    } else if (k == 2)
      entry.transform = {.68f, .62f, .28f, .32f};
    scene().sources.push_back(entry);
    source = int(scene().sources.size()) - 1;
    refreshSources();
    changed();
  }

  void account() {
    QDialog dialog(w);
    dialog.setWindowTitle(QStringLiteral("账号"));
    dialog.setMinimumWidth(440);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    auto state = session->snapshot();
    auto* url = new QLineEdit(q(state.server_url));
    form->addRow(QStringLiteral("服务地址"), url);
    auto* name = new QLineEdit;
    form->addRow(QStringLiteral("用户名"), name);
    auto* password = new QLineEdit;
    password->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("密码"), password);
    auto* registration = new QCheckBox(QStringLiteral("注册新账号"));
    form->addRow(registration);
    auto* guidance = label(QStringLiteral("用户名 3–32 位字母、数字、下划线；密码至少 10 位"));
    layout->addWidget(guidance);
    auto* login = new QPushButton(QStringLiteral("登录 / 注册"));
    layout->addWidget(login);
    auto* user = label("");
    layout->addWidget(user);
    auto* logout = new QPushButton(QStringLiteral("退出账号"));
    layout->addWidget(logout);
    auto* oldPassword = new QLineEdit;
    oldPassword->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("原密码"), oldPassword);
    auto* newPassword = new QLineEdit;
    newPassword->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("新密码"), newPassword);
    auto* change = new QPushButton(QStringLiteral("修改密码"));
    layout->addWidget(change);
    auto* feedback = label("");
    layout->addWidget(feedback);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(close);
    QObject::connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(login, &QPushButton::clicked, &dialog, [&] {
      session->login(s(url->text()), s(name->text()), s(password->text()),
                     registration->isChecked());
      password->clear();
    });
    QObject::connect(logout, &QPushButton::clicked, &dialog, [&] { session->logout(); });
    QObject::connect(change, &QPushButton::clicked, &dialog, [&] {
      session->change_password(s(oldPassword->text()), s(newPassword->text()));
      oldPassword->clear();
      newPassword->clear();
    });
    QTimer poll;
    auto refresh = [&] {
      auto state = session->snapshot();
      bool authenticated = !state.user.is_null();
      user->setText(authenticated ? QStringLiteral("已登录：") + q(state.user.value("name", ""))
                                  : QStringLiteral("尚未登录"));
      for (auto* edit : {name, password})
        edit->setEnabled(!authenticated);
      registration->setEnabled(!authenticated);
      login->setEnabled(!authenticated && !state.busy && !config.offline);
      logout->setEnabled(authenticated && !state.busy);
      oldPassword->setEnabled(authenticated);
      newPassword->setEnabled(authenticated);
      change->setEnabled(authenticated && !state.busy);
      feedback->setText(config.offline ? QStringLiteral("离屏模式不连接账号服务")
                        : state.busy   ? QStringLiteral("正在处理…")
                                       : q(state.error));
    };
    QObject::connect(&poll, &QTimer::timeout, &dialog, refresh);
    poll.start(200);
    refresh();
    dialog.exec();
  }

  void record() {
    if (session->engine()->snapshot().recording) {
      session->engine()->stop_recording();
      return;
    }
    auto file = QFileDialog::getSaveFileName(w, QStringLiteral("录制为 MP4"), {},
                                             QStringLiteral("MP4 视频 (*.mp4)"));
    if (file.isEmpty())
      return;
    if (!file.endsWith(".mp4", Qt::CaseInsensitive))
      file += ".mp4";
    session->engine()->start_recording(path(file));
  }

  void rebuildInteractions(const Snapshot& state) {
    auto clear = [](QWidget* page) {
      if (auto* layout = page->layout()) {
        while (auto* child = layout->takeAt(0)) {
          delete child->widget();
          delete child;
        }
        delete layout;
      }
    };
    clear(guestPage);
    clear(pkPage);
    auto* guests = new QVBoxLayout(guestPage);
    auto* pkLayout = new QVBoxLayout(pkPage);
    auto command = [this](QVBoxLayout* layout, const QString& text, const std::string& route,
                          Json payload = Json::object()) {
      action(text, {}, layout, [this, route, payload] { session->command(route, payload); });
    };
    if (state.live.is_object()) {
      for (const auto& guest : state.live.value("guests", Json::array())) {
        guests->addWidget(label(q(guest.value("name", ""))));
        auto uid = guest.value("user_id", "");
        auto status = guest.value("state", "");
        if (status == "pending") {
          command(guests, QStringLiteral("接受连麦"), "guest/approve", {{"target", uid}});
          command(guests, QStringLiteral("拒绝连麦"), "guest/reject", {{"target", uid}});
        } else if (status == "approved")
          command(guests, QStringLiteral("结束连麦"), "guest/end", {{"target", uid}});
      }
      guests->addWidget(label(QStringLiteral("在线成员")));
      for (const auto& viewer : state.live.value("viewers", Json::array())) {
        if (state.user.is_object() && viewer.value("user_id", "") == state.user.value("id", ""))
          continue;
        auto uid = viewer.value("identity", "");
        bool muted = viewer.value("muted", false);
        guests->addWidget(label(q(viewer.value("name", ""))));
        command(guests, muted ? QStringLiteral("解除禁言") : QStringLiteral("禁言"), "mute",
                {{"target", uid}, {"enabled", !muted}});
        command(guests, QStringLiteral("移出"), "kick", {{"target", uid}});
      }
      auto pk = state.live.value("pk", Json());
      if (pk.is_object()) {
        auto ps = pk.value("state", "");
        pkLayout->addWidget(label(QStringLiteral("PK %1 · %2 : %3")
                                      .arg(q(ps))
                                      .arg(pk.value("score_a", 0))
                                      .arg(pk.value("score_b", 0))));
        if (ps == "invited" && pk.value("b", "") == state.broadcast) {
          command(pkLayout, QStringLiteral("接受 PK"), "pk/accept");
          command(pkLayout, QStringLiteral("拒绝 PK"), "pk/reject");
        }
        if (ps == "active" || ps == "invited")
          command(pkLayout, QStringLiteral("退出 PK"), "pk/leave");
      }
    } else
      guests->addWidget(label(QStringLiteral("开播后可管理连麦申请和在线成员。")));
    auto* refresh = action(QStringLiteral("刷新可连线主播"), "refreshRooms", pkLayout,
                           [this] { session->refresh_rooms(); });
    refresh->setEnabled(!config.offline && !state.user.is_null());
    for (const auto& room : state.rooms) {
      if (room.value("state", "") != "live" || room.value("broadcast", "") == state.broadcast)
        continue;
      pkLayout->addWidget(label(q(room.value("title", ""))));
      command(pkLayout, QStringLiteral("邀请 PK"), "pk/invite", {{"target", room.at("broadcast")}});
    }
    pkLayout->addWidget(label(QStringLiteral("本场贡献榜")));
    if (state.live.is_object())
      for (const auto& entry : state.live.value("leaderboard", Json::array()))
        pkLayout->addWidget(label(QStringLiteral("%1 · %2 分")
                                      .arg(q(entry.value("name", "")))
                                      .arg(entry.value("points", 0))));
    guests->addStretch();
    pkLayout->addStretch();
  }

  void tick() {
    auto state = session->snapshot();
    auto media = session->engine()->snapshot();
    auto current = session->engine()->options();
    if (media.preview != lastFrame) {
      lastFrame = media.preview;
      preview->setFrame(lastFrame);
    }
    if (fpsClock.elapsed() >= 1000) {
      fps = media.frames >= lastFrames
                ? double(media.frames - lastFrames) * 1000 / fpsClock.elapsed()
                : 0;
      lastFrames = media.frames;
      fpsClock.restart();
    }
    bool live = !state.broadcast.empty();
    liveButton->setText(live ? QStringLiteral("结束直播") : QStringLiteral("开始直播"));
    liveButton->setEnabled(!config.offline && !state.busy && !state.user.is_null());
    accountButton->setText(state.user.is_null() ? QStringLiteral("登录 / 注册")
                                                : q(state.user.value("name", "账号")));
    recordButton->setText(media.recording ? QStringLiteral("停止录制")
                                          : QStringLiteral("本地录制"));
    format->setEnabled(!live && !media.recording && !state.busy);
    shareButton->setEnabled(state.live.is_object());
    message->setEnabled(live);
    w->findChild<QPushButton*>("sendMessage")->setEnabled(live && !state.busy);
    micPeak->setValue(int(std::clamp(media.microphone_peak, 0.f, 1.f) * 100));
    systemPeak->setValue(int(std::clamp(media.system_peak, 0.f, 1.f) * 100));
    online->setText(state.live.is_object()
                        ? QStringLiteral("在线 %1 人").arg(state.live.value("online", 0))
                        : QStringLiteral("直播互动 · 未开播"));
    if (oldEvents != state.events) {
      oldEvents = state.events;
      auto* bar = messages->verticalScrollBar();
      bool bottom = bar->value() >= bar->maximum() - 10;
      int position = bar->value();
      QStringList lines;
      for (const auto& event : state.events) {
        auto kind = event.value("kind", "");
        auto p = event.value("payload", Json::object());
        if (kind == "chat")
          lines << q(p.value("name", "")) + QStringLiteral("：") + q(p.value("text", ""));
        else if (kind == "gift")
          lines << QStringLiteral("%1 送出 %2 分礼物")
                       .arg(q(p.value("name", "")))
                       .arg(p.value("points", 0));
      }
      messages->setPlainText(lines.join('\n'));
      bar->setValue(bottom ? bar->maximum() : position);
    }
    auto user = state.user.dump();
    if (oldLive != state.live || oldRooms != state.rooms || oldUser != user) {
      oldLive = state.live;
      oldRooms = state.rooms;
      oldUser = user;
      rebuildInteractions(state);
    }
    status->setText(QStringLiteral("%1  |  %2 × %3 · %4 fps  |  已合成 %5 帧 · 丢帧 %6%7%8")
                        .arg(q(state.state))
                        .arg(current.width)
                        .arg(current.height)
                        .arg(fps, 0, 'f', 1)
                        .arg(media.frames)
                        .arg(media.dropped)
                        .arg(media.recording ? QStringLiteral("  |  正在录制") : QString())
                        .arg(live ? QStringLiteral("  |  发送 %1 fps / %2 kbps")
                                        .arg(state.publish_fps, 0, 'f', 1)
                                        .arg(state.bitrate_kbps, 0, 'f', 0)
                                  : QString()));
    QStringList errors;
    if (!error.isEmpty())
      errors << error;
    if (!state.error.empty())
      errors << q(state.error);
    if (!media.error.empty())
      errors << q(media.error);
    for (const auto& [key, text] : media.source_errors)
      errors << q(text);
    errorLabel->setText(errors.join('\n'));
    errorLabel->setVisible(!errors.empty());
  }
};

Window::Window(WindowOptions options, QWidget* parent)
    : QMainWindow(parent), impl_(std::make_unique<Impl>(this, std::move(options))) {}

Window::~Window() {
  impl_->save();
}

sdk::StudioSnapshot Window::mediaSnapshot() const {
  return impl_->session->engine()->snapshot();
}

bool Window::saveProject() {
  return impl_->save();
}

void Window::closeEvent(QCloseEvent* event) {
  if (!impl_->config.offline &&
      (!impl_->session->snapshot().broadcast.empty() || mediaSnapshot().recording)) {
    if (QMessageBox::question(this, QStringLiteral("退出直播助手"),
                              QStringLiteral("直播或录制正在进行，确定结束并退出？")) !=
        QMessageBox::Yes) {
      event->ignore();
      return;
    }
    impl_->session->stop();
    impl_->session->engine()->stop_recording();
  }
  if (!saveProject()) {
    event->ignore();
    return;
  }
  event->accept();
}
}  // namespace studio_app
