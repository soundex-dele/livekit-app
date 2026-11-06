#include "av/ui.hpp"
#include "window_theme.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QProxyStyle>
#include <QScreen>
#include <QScrollArea>
#include <QShowEvent>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QVBoxLayout>
#include <algorithm>

static void initialize_ui_resources() {
  Q_INIT_RESOURCE(av_ui_assets);
}

namespace av::ui {
namespace {

// Keep native widget keyboard/accessibility semantics while painting the small
// indicators consistently, including the Qt offscreen platform.
class ControlStyle final : public QProxyStyle {
 public:
  ControlStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}

  void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                     const QWidget* widget = nullptr) const override {
    const bool check = element == PE_IndicatorCheckBox;
    const bool radio = element == PE_IndicatorRadioButton;
    const bool arrow = element == PE_IndicatorArrowDown || element == PE_IndicatorArrowUp ||
                       element == PE_IndicatorArrowLeft || element == PE_IndicatorArrowRight;
    if (!check && !radio && !arrow) {
      QProxyStyle::drawPrimitive(element, option, painter, widget);
      return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    QColor foreground = option->palette.color(QPalette::Text);
    if (!(option->state & State_Enabled)) {
      foreground = option->palette.color(QPalette::Disabled, QPalette::Text);
    }
    const QPointF center = option->rect.center();
    if (arrow) {
      const qreal extent =
          std::clamp(std::min(option->rect.width(), option->rect.height()) / 3.0, 2.0, 4.0);
      QPolygonF triangle;
      if (element == PE_IndicatorArrowDown) {
        triangle << center + QPointF(-extent, -extent / 2) << center + QPointF(extent, -extent / 2)
                 << center + QPointF(0, extent / 2);
      } else if (element == PE_IndicatorArrowUp) {
        triangle << center + QPointF(-extent, extent / 2) << center + QPointF(extent, extent / 2)
                 << center + QPointF(0, -extent / 2);
      } else if (element == PE_IndicatorArrowRight) {
        triangle << center + QPointF(-extent / 2, -extent) << center + QPointF(-extent / 2, extent)
                 << center + QPointF(extent / 2, 0);
      } else {
        triangle << center + QPointF(extent / 2, -extent) << center + QPointF(extent / 2, extent)
                 << center + QPointF(-extent / 2, 0);
      }
      painter->setPen(Qt::NoPen);
      painter->setBrush(foreground);
      painter->drawPolygon(triangle);
    } else {
      const QRectF box = QRectF(option->rect).adjusted(1.5, 1.5, -1.5, -1.5);
      const bool selected = option->state & (State_On | State_NoChange);
      const QColor accent("#2563EB");
      painter->setPen(QPen(selected ? accent : foreground, 1.4));
      painter->setBrush(selected ? accent : option->palette.color(QPalette::Base));
      if (radio) {
        painter->drawEllipse(box);
      } else {
        painter->drawRoundedRect(box, 3, 3);
      }
      if (selected) {
        painter->setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (radio) {
          painter->setBrush(Qt::white);
          painter->drawEllipse(center, 2.5, 2.5);
        } else if (option->state & State_NoChange) {
          painter->drawLine(center + QPointF(-4, 0), center + QPointF(4, 0));
        } else {
          painter->drawLine(center + QPointF(-4, 0), center + QPointF(-1, 3));
          painter->drawLine(center + QPointF(-1, 3), center + QPointF(5, -4));
        }
      }
    }
    painter->restore();
  }
};

void refresh_style(QWidget* widget) {
  if (widget) {
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
  }
}

const char* tone_name(Tone tone) {
  switch (tone) {
    case Tone::Info:
      return "info";
    case Tone::Success:
      return "success";
    case Tone::Warning:
      return "warning";
    case Tone::Error:
      return "error";
    default:
      return "neutral";
  }
}

QString button_text(QMessageBox::StandardButton button) {
  switch (button) {
    case QMessageBox::Ok:
      return QStringLiteral("确定");
    case QMessageBox::Save:
      return QStringLiteral("保存");
    case QMessageBox::SaveAll:
      return QStringLiteral("全部保存");
    case QMessageBox::Open:
      return QStringLiteral("打开");
    case QMessageBox::Yes:
      return QStringLiteral("确认");
    case QMessageBox::YesToAll:
      return QStringLiteral("全部确认");
    case QMessageBox::No:
      return QStringLiteral("取消");
    case QMessageBox::NoToAll:
      return QStringLiteral("全部取消");
    case QMessageBox::Abort:
      return QStringLiteral("终止");
    case QMessageBox::Retry:
      return QStringLiteral("重试");
    case QMessageBox::Ignore:
      return QStringLiteral("忽略");
    case QMessageBox::Close:
      return QStringLiteral("关闭");
    case QMessageBox::Cancel:
      return QStringLiteral("取消");
    case QMessageBox::Discard:
      return QStringLiteral("放弃");
    case QMessageBox::Help:
      return QStringLiteral("帮助");
    case QMessageBox::Apply:
      return QStringLiteral("应用");
    case QMessageBox::Reset:
      return QStringLiteral("重置");
    case QMessageBox::RestoreDefaults:
      return QStringLiteral("恢复默认");
    default:
      return QStringLiteral("确定");
  }
}

QMessageBox::StandardButton show_message(QWidget* parent, const QString& title, const QString& text,
                                         Tone tone, QMessageBox::StandardButtons buttons,
                                         QMessageBox::StandardButton default_button) {
  Dialog dialog(parent, title);
  dialog.setObjectName("avMessageDialog");
  auto* message = new Notice(text, tone, dialog.body());
  message->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
  dialog.content_layout()->addWidget(message);
  auto result = buttons.testFlag(QMessageBox::Cancel)  ? QMessageBox::Cancel
                : buttons.testFlag(QMessageBox::No)    ? QMessageBox::No
                : buttons.testFlag(QMessageBox::Close) ? QMessageBox::Close
                                                       : QMessageBox::NoButton;
  if (default_button == QMessageBox::NoButton) {
    default_button = result != QMessageBox::NoButton ? result : QMessageBox::Ok;
  }
  const QMessageBox::StandardButton order[] = {
      QMessageBox::Cancel,          QMessageBox::No,     QMessageBox::NoToAll,
      QMessageBox::Close,           QMessageBox::Help,   QMessageBox::Discard,
      QMessageBox::Abort,           QMessageBox::Ignore, QMessageBox::Reset,
      QMessageBox::RestoreDefaults, QMessageBox::Retry,  QMessageBox::Open,
      QMessageBox::SaveAll,         QMessageBox::Save,   QMessageBox::Apply,
      QMessageBox::YesToAll,        QMessageBox::Yes,    QMessageBox::Ok};
  for (auto standard : order) {
    if (!buttons.testFlag(standard)) {
      continue;
    }
    const bool cancel = standard == QMessageBox::Cancel || standard == QMessageBox::No ||
                        standard == QMessageBox::NoToAll || standard == QMessageBox::Close;
    const bool danger = standard == QMessageBox::Discard || standard == QMessageBox::Abort ||
                        (tone == Tone::Warning && standard == QMessageBox::Yes);
    auto* action = dialog.add_action(button_text(standard), cancel   ? ButtonRole::Secondary
                                                            : danger ? ButtonRole::Danger
                                                                     : ButtonRole::Primary);
    action->setObjectName(QStringLiteral("avDialogAction%1").arg(static_cast<int>(standard)));
    if (standard == default_button) {
      action->setDefault(true);
      action->setFocus();
    }
    QObject::connect(action, &QPushButton::clicked, &dialog, [&dialog, &result, standard] {
      result = standard;
      dialog.accept();
    });
  }
  dialog.exec();
  return result;
}

void add_input_actions(Dialog& dialog) {
  auto* cancel = dialog.add_action(QStringLiteral("取消"));
  auto* accept = dialog.add_action(QStringLiteral("确定"), ButtonRole::Primary);
  accept->setDefault(true);
  QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  QObject::connect(accept, &QPushButton::clicked, &dialog, &QDialog::accept);
}

}  // namespace

QString style_sheet(Theme theme) {
  initialize_ui_resources();
  const bool dark = theme == Theme::Dark;
  QString sheet = QString::fromUtf8(R"(
    QWidget { color: @text; font-family: "Segoe UI", "Microsoft YaHei UI", "PingFang SC", sans-serif; font-size: 14px; }
    QWidget[uiTheme] { background: @background; }
    QWidget[uiSurface="card"] { background: @surface; border: 1px solid @border; border-radius: 12px; }
    QWidget[uiSurface="toolbar"] { background: @surface; border: none; border-bottom: 1px solid @border; }
    QWidget[uiSurface="sidebar"] { background: @surface; border: none; border-right: 1px solid @border; }
    QWidget[uiSurface="media"] { background: #0B1220; color: #E7EDF7; border: none; border-radius: 12px; }
    QLabel { background: transparent; border: none; }
    QLabel[uiText="title"] { font-size: 24px; font-weight: 600; color: @text; }
    QLabel[uiText="heading"] { font-size: 16px; font-weight: 600; color: @text; }
    QLabel[uiText="muted"] { color: @muted; }
    QLabel[uiText="caption"] { font-size: 12px; color: @muted; }
    QPushButton, QToolButton { background: @surface; border: 1px solid @border; border-radius: 8px; padding: 8px 14px; min-height: 18px; color: @text; }
    QToolButton { padding: 6px; }
    QPushButton:hover, QToolButton:hover { background: @hover; border-color: @accent; }
    QPushButton:pressed, QToolButton:pressed { background: @selected; }
    QPushButton:checked, QToolButton:checked { background: @selected; color: @accent; border-color: @accent; }
    QPushButton[uiButton="primary"], QToolButton[uiButton="primary"] { background: #2563EB; color: white; border-color: #2563EB; font-weight: 600; }
    QPushButton[uiButton="primary"]:hover, QToolButton[uiButton="primary"]:hover { background: #1D4ED8; border-color: #1D4ED8; }
    QPushButton[uiButton="primary"]:pressed { background: #1E40AF; }
    QPushButton[uiButton="danger"], QToolButton[uiButton="danger"] { color: @error; background: @errorBackground; border-color: @error; }
    QPushButton[uiButton="danger"]:hover { color: white; background: #B91C1C; }
    QPushButton[uiButton="ghost"], QToolButton[uiButton="ghost"] { background: transparent; border-color: transparent; }
    QPushButton[uiButton="ghost"]:hover, QToolButton[uiButton="ghost"]:hover { background: @hover; }
    QPushButton[uiButton="ghost"]:checked, QToolButton[uiButton="ghost"]:checked { background: @selected; border-color: @accent; color: @accent; }
    QPushButton:focus, QToolButton:focus { border: 2px solid @accent; padding: 7px 13px; }
    QToolButton:focus { padding: 5px; }
    QPushButton:disabled, QToolButton:disabled { color: @disabled; background: @subtle; border-color: @border; }
    QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QSpinBox, QDoubleSpinBox, QDateTimeEdit, QDateEdit, QTimeEdit {
      background: @input; color: @text; border: 1px solid @inputBorder; border-radius: 8px; padding: 8px 10px; selection-background-color: #2563EB; selection-color: white;
    }
    QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QDateTimeEdit:focus { border: 2px solid @accent; padding: 7px 9px; }
    QLineEdit:disabled, QTextEdit:disabled, QComboBox:disabled, QSpinBox:disabled { background: @subtle; color: @disabled; border-color: @border; }
    QLineEdit[uiTone="error"] { border-color: @error; }
    QComboBox { padding-right: 28px; }
    QComboBox::drop-down, QDateTimeEdit::drop-down { width: 24px; border: none; background: @subtle; border-top-right-radius: 8px; border-bottom-right-radius: 8px; }
    QComboBox::down-arrow, QDateTimeEdit::down-arrow, QSpinBox::up-arrow, QSpinBox::down-arrow, QDoubleSpinBox::up-arrow, QDoubleSpinBox::down-arrow { width: 12px; height: 12px; }
    QComboBox::down-arrow, QDateTimeEdit::down-arrow, QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(:/av/ui/down-@mode.xpm); }
    QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(:/av/ui/up-@mode.xpm); }
    QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 20px; border-left: 1px solid @border; border-bottom: 1px solid @border; border-top-right-radius: 8px; background: @subtle; }
    QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 20px; border-left: 1px solid @border; border-bottom-right-radius: 8px; background: @subtle; }
    QComboBox QAbstractItemView { background: @surface; color: @text; border: 1px solid @border; selection-background-color: #2563EB; selection-color: white; padding: 4px; outline: 0; }
    QCheckBox, QRadioButton { spacing: 8px; padding: 4px 0; background: transparent; }
    QCheckBox:disabled, QRadioButton:disabled { color: @disabled; }
    QCheckBox:focus, QRadioButton:focus { color: @accent; }
    QCheckBox::indicator, QRadioButton::indicator { width: 18px; height: 18px; }
    QListView, QTreeView, QTableView, QListWidget, QTreeWidget { background: @surface; alternate-background-color: @subtle; color: @text; border: 1px solid @border; border-radius: 8px; outline: 0; }
    QAbstractItemView::item { padding: 8px; min-height: 20px; }
    QAbstractItemView::item:hover { background: @hover; }
    QAbstractItemView::item:selected { background: @selected; color: @accent; }
    QAbstractItemView:focus { border-color: @accent; }
    QHeaderView::section { background: @subtle; color: @muted; border: none; border-bottom: 1px solid @border; padding: 10px; font-weight: 600; }
    QGroupBox { border: 1px solid @border; border-radius: 12px; margin-top: 18px; padding: 16px 12px 12px; font-weight: 600; }
    QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; }
    QTabWidget::pane { border: 1px solid @border; border-radius: 8px; background: @surface; }
    QTabBar::tab { background: transparent; color: @muted; padding: 10px 16px; border-bottom: 2px solid transparent; }
    QTabBar::tab:selected { color: @accent; border-bottom-color: @accent; font-weight: 600; }
    QTabBar::tab:hover { background: @hover; }
    QMenuBar { background: @surface; border-bottom: 1px solid @border; padding: 3px; }
    QMenuBar::item { padding: 6px 12px; background: transparent; border-radius: 6px; }
    QMenuBar::item:selected { background: @hover; }
    QMenu { background: @surface; color: @text; padding: 6px; border: 1px solid @border; border-radius: 8px; }
    QMenu::item { padding: 8px 28px 8px 16px; border-radius: 6px; }
    QMenu::item:selected { background: @selected; color: @accent; }
    QMenu::item:disabled { color: @disabled; }
    QMenu::separator { height: 1px; margin: 5px 8px; background: @border; }
    QToolTip { background: @text; color: @surface; border: none; border-radius: 6px; padding: 6px 10px; }
    QScrollArea { border: none; background: transparent; }
    QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
    QScrollBar::handle:vertical { background: @scroll; min-height: 28px; border-radius: 3px; }
    QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
    QScrollBar::handle:horizontal { background: @scroll; min-width: 28px; border-radius: 3px; }
    QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
    QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
    QSlider::groove:horizontal { height: 4px; border-radius: 2px; background: @border; }
    QSlider::sub-page:horizontal { background: #2563EB; border-radius: 2px; }
    QSlider::handle:horizontal { width: 12px; margin: -4px 0; border-radius: 6px; background: @accent; }
    QSlider:focus { border: 1px solid @accent; border-radius: 6px; }
    QProgressBar { background: @subtle; border: none; border-radius: 4px; text-align: center; }
    QProgressBar::chunk { background: #2563EB; border-radius: 4px; }
    QSplitter::handle { background: @border; width: 1px; height: 1px; }
    QLabel[uiBadge="true"] { background: @subtle; border-radius: 10px; padding: 5px 10px; font-size: 12px; font-weight: 600; }
    QLabel[uiNotice="true"] { background: @subtle; border-radius: 8px; padding: 12px; }
    QLabel[uiTone="neutral"] { color: @muted; }
    QLabel[uiTone="info"] { color: @accent; background: @selected; }
    QLabel[uiTone="success"] { color: @success; background: @successBackground; }
    QLabel[uiTone="warning"] { color: @warning; background: @warningBackground; }
    QLabel[uiTone="error"] { color: @error; background: @errorBackground; }
    QDialog[uiDialog="true"] { background: @surface; }
    QWidget#avDialogFooter { background: @subtle; border-top: 1px solid @border; }
    QWidget#avDialogBody { background: transparent; }
  )");
  const QPair<const char*, QString> values[] = {
      {"@mode", dark ? "dark" : "light"},
      {"@background", dark ? "#0B1220" : "#F5F7FB"},
      {"@surface", dark ? "#151F30" : "#FFFFFF"},
      {"@text", dark ? "#E7EDF7" : "#17243B"},
      {"@muted", dark ? "#ADBCD1" : "#53627A"},
      {"@disabled", dark ? "#7F8FA7" : "#68778E"},
      {"@subtle", dark ? "#1C283B" : "#EDF1F7"},
      {"@hover", dark ? "#26364F" : "#EDF3FF"},
      {"@selected", dark ? "#1E355B" : "#E8F0FF"},
      {"@accent", dark ? "#93BBFF" : "#1D4ED8"},
      {"@inputBorder", dark ? "#63748F" : "#8492A7"},
      {"@input", dark ? "#101A2A" : "#FFFFFF"},
      {"@border", dark ? "#34435A" : "#D5DEEB"},
      {"@scroll", dark ? "#546681" : "#A7B5C9"},
      {"@errorBackground", dark ? "#3B202B" : "#FFF0F1"},
      {"@error", dark ? "#FFB2BA" : "#B42338"},
      {"@warningBackground", dark ? "#382B16" : "#FFF6DF"},
      {"@warning", dark ? "#F9D488" : "#805000"},
      {"@successBackground", dark ? "#11352D" : "#E8F6EE"},
      {"@success", dark ? "#8AE2B7" : "#17633C"},
  };
  for (const auto& value : values) {
    sheet.replace(QLatin1String(value.first), value.second);
  }
  return sheet;
}

void apply_theme(QWidget* root, Theme theme) {
  if (!root) {
    return;
  }
  // The native Windows style omits several indicator primitives when rendered
  // offscreen. Use one portable base style for application-owned controls;
  // system file dialogs remain native. Palettes still belong to each subtree.
  static const bool base_style_ready = [] {
    QApplication::setStyle(new ControlStyle);
    return true;
  }();
  Q_UNUSED(base_style_ready);
  root->setProperty("uiTheme", theme == Theme::Dark ? "dark" : "light");
  const bool dark = theme == Theme::Dark;
  QPalette palette = root->palette();
  palette.setColor(QPalette::Window, QColor(dark ? "#0B1220" : "#F5F7FB"));
  palette.setColor(QPalette::Base, QColor(dark ? "#101A2A" : "#FFFFFF"));
  palette.setColor(QPalette::AlternateBase, QColor(dark ? "#1C283B" : "#EDF1F7"));
  palette.setColor(QPalette::Button, QColor(dark ? "#151F30" : "#FFFFFF"));
  for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
    palette.setColor(role, QColor(dark ? "#E7EDF7" : "#17243B"));
    palette.setColor(QPalette::Disabled, role, QColor(dark ? "#7F8FA7" : "#68778E"));
  }
  palette.setColor(QPalette::PlaceholderText, QColor(dark ? "#ADBCD1" : "#53627A"));
  palette.setColor(QPalette::Highlight, QColor("#2563EB"));
  palette.setColor(QPalette::HighlightedText, Qt::white);
  root->setPalette(palette);
  root->setStyleSheet(style_sheet(theme));
  detail::install_window_theme(root);
}

Theme theme_for(const QWidget* widget) {
  while (widget) {
    const auto value = widget->property("uiTheme").toString();
    if (!value.isEmpty()) {
      return value == "dark" ? Theme::Dark : Theme::Light;
    }
    widget = widget->parentWidget();
  }
  return Theme::Light;
}

void set_button_role(QAbstractButton* button, ButtonRole role) {
  if (!button) {
    return;
  }
  const char* value = role == ButtonRole::Primary  ? "primary"
                      : role == ButtonRole::Danger ? "danger"
                      : role == ButtonRole::Ghost  ? "ghost"
                                                   : "secondary";
  button->setProperty("uiButton", value);
  button->setCursor(Qt::PointingHandCursor);
  refresh_style(button);
}

void set_surface(QWidget* widget, const char* role) {
  if (widget) {
    widget->setProperty("uiSurface", role);
    widget->setAttribute(Qt::WA_StyledBackground);
    refresh_style(widget);
  }
}

void set_text_role(QLabel* label, const char* role) {
  if (label) {
    label->setProperty("uiText", role);
    refresh_style(label);
  }
}

void set_tone(QWidget* widget, Tone tone) {
  if (widget) {
    if (widget->property("uiTone").toString() == QLatin1String(tone_name(tone))) {
      return;
    }
    widget->setProperty("uiTone", tone_name(tone));
    refresh_style(widget);
  }
}

QIcon icon(Icon kind, QColor color, int size) {
  QPixmap pixmap(size * 2, size * 2);
  pixmap.setDevicePixelRatio(2);
  pixmap.fill(Qt::transparent);
  QPainter p(&pixmap);
  p.setRenderHint(QPainter::Antialiasing);
  p.scale(size / 24.0, size / 24.0);
  p.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  auto line = [&p](int x1, int y1, int x2, int y2) { p.drawLine(x1, y1, x2, y2); };
  switch (kind) {
    case Icon::Plus:
      line(5, 12, 19, 12);
      line(12, 5, 12, 19);
      break;
    case Icon::Close:
      line(6, 6, 18, 18);
      line(18, 6, 6, 18);
      break;
    case Icon::Check:
      line(5, 12, 10, 17);
      line(10, 17, 19, 7);
      break;
    case Icon::Info:
    case Icon::Error:
      p.drawEllipse(QRectF(3, 3, 18, 18));
      line(12, 10, 12, 16);
      p.drawPoint(12, 7);
      break;
    case Icon::Warning: {
      const QPointF points[] = {{12, 3}, {22, 21}, {2, 21}, {12, 3}};
      p.drawPolyline(points, 4);
      line(12, 9, 12, 14);
      p.drawPoint(12, 18);
      break;
    }
    case Icon::Settings:
      p.drawEllipse(QRectF(5, 5, 14, 14));
      p.drawEllipse(QRectF(9, 9, 6, 6));
      line(12, 2, 12, 5);
      line(12, 19, 12, 22);
      line(2, 12, 5, 12);
      line(19, 12, 22, 12);
      break;
    case Icon::Home: {
      const QPointF points[] = {{3, 11},  {12, 3},  {21, 11}, {19, 11}, {19, 21}, {14, 21},
                                {14, 15}, {10, 15}, {10, 21}, {5, 21},  {5, 11}};
      p.drawPolyline(points, 11);
      break;
    }
    case Icon::Video:
      p.drawRoundedRect(QRectF(2, 6, 14, 12), 2, 2);
      line(16, 10, 22, 6);
      line(22, 6, 22, 18);
      line(22, 18, 16, 14);
      break;
    case Icon::Mic:
      p.drawRoundedRect(QRectF(9, 2, 6, 13), 3, 3);
      p.drawArc(QRectF(5, 7, 14, 12), 180 * 16, 180 * 16);
      line(12, 19, 12, 22);
      line(8, 22, 16, 22);
      break;
    case Icon::Screen:
      p.drawRoundedRect(QRectF(2, 3, 20, 14), 2, 2);
      line(12, 17, 12, 21);
      line(7, 21, 17, 21);
      break;
    case Icon::Play: {
      const QPointF points[] = {{8, 4}, {20, 12}, {8, 20}};
      p.setBrush(color);
      p.drawPolygon(points, 3);
      break;
    }
    case Icon::Pause:
      p.drawRoundedRect(6, 4, 4, 16, 1, 1);
      p.drawRoundedRect(14, 4, 4, 16, 1, 1);
      break;
    case Icon::Stop:
      p.drawRoundedRect(5, 5, 14, 14, 2, 2);
      break;
    case Icon::Folder: {
      const QPointF points[] = {{3, 6}, {9, 6}, {11, 9}, {21, 9}, {21, 20}, {3, 20}, {3, 6}};
      p.drawPolyline(points, 7);
      break;
    }
    case Icon::ChevronRight:
      line(9, 5, 16, 12);
      line(16, 12, 9, 19);
      break;
    case Icon::Copy:
      p.drawRoundedRect(8, 8, 13, 13, 2, 2);
      line(3, 16, 3, 3);
      line(3, 3, 16, 3);
      break;
    case Icon::Users:
      p.drawEllipse(7, 3, 7, 7);
      p.drawArc(QRectF(3, 13, 16, 16), 0, 180 * 16);
      p.drawArc(QRectF(15, 4, 6, 6), -90 * 16, 180 * 16);
      line(20, 14, 22, 19);
      break;
    case Icon::Link:
      p.drawRoundedRect(QRectF(2, 8, 10, 8), 4, 4);
      p.drawRoundedRect(QRectF(12, 8, 10, 8), 4, 4);
      line(8, 12, 16, 12);
      break;
  }
  return QIcon(pixmap);
}

Card::Card(QWidget* parent) : QFrame(parent) {
  set_surface(this);
}

StatusBadge::StatusBadge(const QString& text, QWidget* parent) : QLabel(text, parent) {
  setProperty("uiBadge", true);
  setTextFormat(Qt::PlainText);
  setAlignment(Qt::AlignCenter);
  setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
  set_tone(this, Tone::Neutral);
}

void StatusBadge::set_status(const QString& text, Tone tone) {
  setText(text);
  set_tone(this, tone);
}

Notice::Notice(const QString& text, Tone tone, QWidget* parent) : QLabel(parent) {
  setProperty("uiNotice", true);
  setWordWrap(true);
  setTextFormat(Qt::PlainText);
  set_message(text, tone);
}

void Notice::set_message(const QString& text, Tone tone) {
  setText(text);
  set_tone(this, tone);
  setVisible(!text.isEmpty());
}

EmptyState::EmptyState(const QString& title, const QString& description, QWidget* parent)
    : QWidget(parent), title_(new QLabel(this)), description_(new QLabel(this)) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 32, 24, 32);
  layout->setSpacing(12);
  title_->setAlignment(Qt::AlignCenter);
  description_->setAlignment(Qt::AlignCenter);
  for (auto* label : {title_, description_}) {
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
  }
  set_text_role(title_, "heading");
  set_text_role(description_, "muted");
  layout->addStretch();
  layout->addWidget(title_);
  layout->addWidget(description_);
  layout->addStretch();
  set_text(title, description);
}

void EmptyState::set_text(const QString& title, const QString& description) {
  title_->setText(title);
  description_->setText(description);
}

Dialog::Dialog(QWidget* parent, const QString& title, const QString& description)
    : QDialog(parent), opener_(QApplication::focusWidget()) {
  setProperty("uiDialog", true);
  setWindowTitle(title);
  setWindowModality(Qt::WindowModal);
  setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  apply_theme(this, theme_for(parent));
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);
  auto* header = new QWidget(this);
  auto* heading = new QVBoxLayout(header);
  heading->setContentsMargins(24, 24, 24, 16);
  heading->setSpacing(8);
  auto* title_label = new QLabel(title, header);
  title_label->setObjectName("avDialogTitle");
  title_label->setTextFormat(Qt::PlainText);
  title_label->setWordWrap(true);
  set_text_role(title_label, "title");
  heading->addWidget(title_label);
  if (!description.isEmpty()) {
    auto* subtitle = new QLabel(description, header);
    subtitle->setTextFormat(Qt::PlainText);
    subtitle->setWordWrap(true);
    set_text_role(subtitle, "muted");
    heading->addWidget(subtitle);
  }
  root->addWidget(header);
  auto* scroll = new QScrollArea(this);
  scroll->setObjectName("avDialogScroll");
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setFrameShape(QFrame::NoFrame);
  body_ = new QWidget(scroll);
  body_->setObjectName("avDialogBody");
  content_ = new QVBoxLayout(body_);
  content_->setContentsMargins(24, 4, 24, 24);
  content_->setSpacing(16);
  scroll->setWidget(body_);
  root->addWidget(scroll, 1);
  error_ = new Notice({}, Tone::Error, this);
  error_->setObjectName("avDialogError");
  root->addWidget(error_);
  footer_ = new QWidget(this);
  footer_->setObjectName("avDialogFooter");
  footer_->setAttribute(Qt::WA_StyledBackground);
  actions_ = new QHBoxLayout(footer_);
  actions_->setContentsMargins(24, 16, 24, 16);
  actions_->setSpacing(8);
  actions_->addStretch();
  root->addWidget(footer_);
  setMinimumWidth(360);
  resize(520, 360);
}

QVBoxLayout* Dialog::content_layout() const {
  return content_;
}

QHBoxLayout* Dialog::button_layout() const {
  return actions_;
}

QWidget* Dialog::body() const {
  return body_;
}

QPushButton* Dialog::add_action(const QString& text, ButtonRole role) {
  auto* button = new QPushButton(text, footer_);
  button->setMinimumWidth(88);
  button->setAutoDefault(false);
  set_button_role(button, role);
  actions_->addWidget(button);
  return button;
}

void Dialog::set_error(const QString& text) {
  error_->set_message(text, Tone::Error);
}

void Dialog::set_busy(bool busy) {
  busy_ = busy;
  body_->setEnabled(!busy);
  footer_->setEnabled(!busy);
  setCursor(busy ? Qt::WaitCursor : Qt::ArrowCursor);
}

void Dialog::reject() {
  if (!busy_) {
    QDialog::reject();
  }
}

void Dialog::done(int result) {
  const QPointer<QWidget> opener = opener_;
  QDialog::done(result);
  if (opener && opener->isVisible() && opener->isEnabled()) {
    opener->setFocus(Qt::OtherFocusReason);
  }
}

void Dialog::showEvent(QShowEvent* event) {
  const QRect available = screen()->availableGeometry();
  const int maximum_width = std::max(240, available.width() - 32);
  const int maximum_height = std::max(200, available.height() - 32);
  setMinimumWidth(std::min(360, maximum_width));
  setMaximumSize(maximum_width, maximum_height);
  const int width = std::min(std::max(this->width(), 480), maximum_width);
  const int content_height = content_->hasHeightForWidth() ? content_->heightForWidth(width)
                                                           : content_->sizeHint().height();
  const int height =
      std::clamp(content_height + 190, std::min(260, maximum_height), maximum_height);
  resize(width, height);
  if (parentWidget()) {
    const QPoint center = parentWidget()->mapToGlobal(parentWidget()->rect().center());
    move(std::clamp(center.x() - width / 2, available.left(), available.right() - width + 1),
         std::clamp(center.y() - height / 2, available.top(), available.bottom() - height + 1));
  }
  QDialog::showEvent(event);
}

QMessageBox::StandardButton MessageBox::question(QWidget* parent, const QString& title,
                                                 const QString& text,
                                                 QMessageBox::StandardButtons buttons,
                                                 QMessageBox::StandardButton default_button) {
  return show_message(parent, title, text, Tone::Info, buttons, default_button);
}

QMessageBox::StandardButton MessageBox::information(QWidget* parent, const QString& title,
                                                    const QString& text,
                                                    QMessageBox::StandardButtons buttons,
                                                    QMessageBox::StandardButton default_button) {
  return show_message(parent, title, text, Tone::Info, buttons, default_button);
}

QMessageBox::StandardButton MessageBox::warning(QWidget* parent, const QString& title,
                                                const QString& text,
                                                QMessageBox::StandardButtons buttons,
                                                QMessageBox::StandardButton default_button) {
  return show_message(parent, title, text, Tone::Warning, buttons, default_button);
}

QMessageBox::StandardButton MessageBox::critical(QWidget* parent, const QString& title,
                                                 const QString& text,
                                                 QMessageBox::StandardButtons buttons,
                                                 QMessageBox::StandardButton default_button) {
  return show_message(parent, title, text, Tone::Error, buttons, default_button);
}

QString InputDialog::getText(QWidget* parent, const QString& title, const QString& label,
                             QLineEdit::EchoMode mode, const QString& text, bool* ok,
                             Qt::WindowFlags flags, Qt::InputMethodHints hints) {
  Dialog dialog(parent, title);
  if (flags != Qt::WindowFlags{}) {
    dialog.setWindowFlags(flags);
  }
  auto* field = new QLineEdit(text, dialog.body());
  field->setEchoMode(mode);
  field->setInputMethodHints(hints);
  field->setAccessibleName(label);
  auto* caption = new QLabel(label, dialog.body());
  caption->setTextFormat(Qt::PlainText);
  caption->setWordWrap(true);
  caption->setBuddy(field);
  dialog.content_layout()->addWidget(caption);
  dialog.content_layout()->addWidget(field);
  add_input_actions(dialog);
  field->selectAll();
  field->setFocus();
  const bool accepted = dialog.exec() == QDialog::Accepted;
  if (ok) {
    *ok = accepted;
  }
  return accepted ? field->text() : QString{};
}

QString InputDialog::getItem(QWidget* parent, const QString& title, const QString& label,
                             const QStringList& items, int current, bool editable, bool* ok,
                             Qt::WindowFlags flags, Qt::InputMethodHints hints) {
  Dialog dialog(parent, title);
  if (flags != Qt::WindowFlags{}) {
    dialog.setWindowFlags(flags);
  }
  auto* field = new QComboBox(dialog.body());
  field->addItems(items);
  field->setEditable(editable);
  field->setCurrentIndex(current);
  field->setInputMethodHints(hints);
  field->setAccessibleName(label);
  auto* caption = new QLabel(label, dialog.body());
  caption->setWordWrap(true);
  caption->setTextFormat(Qt::PlainText);
  caption->setBuddy(field);
  dialog.content_layout()->addWidget(caption);
  dialog.content_layout()->addWidget(field);
  add_input_actions(dialog);
  field->setFocus();
  const bool accepted = dialog.exec() == QDialog::Accepted;
  if (ok) {
    *ok = accepted;
  }
  return accepted ? field->currentText() : QString{};
}

QColor choose_color(const QColor& initial, QWidget* parent, const QString& title) {
  QColorDialog dialog(initial, parent);
  dialog.setOption(QColorDialog::DontUseNativeDialog);
  dialog.setOption(QColorDialog::ShowAlphaChannel);
  dialog.setWindowTitle(title.isEmpty() ? QStringLiteral("选择颜色") : title);
  apply_theme(&dialog, theme_for(parent));
  if (auto* buttons = dialog.findChild<QDialogButtonBox*>()) {
    if (auto* accept = buttons->button(QDialogButtonBox::Ok)) {
      accept->setText(QStringLiteral("应用颜色"));
      set_button_role(accept, ButtonRole::Primary);
    }
    if (auto* cancel = buttons->button(QDialogButtonBox::Cancel)) {
      cancel->setText(QStringLiteral("取消"));
    }
  }
  return dialog.exec() == QDialog::Accepted ? dialog.selectedColor() : QColor{};
}

}  // namespace av::ui
