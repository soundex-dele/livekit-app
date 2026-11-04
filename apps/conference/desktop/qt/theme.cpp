#include "theme.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <algorithm>

namespace meeting::ui {
namespace {

const QColor kBrand{"#006eff"};
const QColor kBrandLight{"#eaf2ff"};
const QColor kInk{"#1f2329"};
const QColor kSubtle{"#8a9099"};
const QColor kLine{"#e5e8ed"};
const QColor kOnDark{"#e7e9ee"};
const QColor kDanger{"#f04b4b"};

void stroke(QPainter& p, QColor color, const QRectF& b) {
  p.setPen(QPen(color, std::max(1.4, b.width() / 18.0), Qt::SolidLine, Qt::RoundCap,
                Qt::RoundJoin));
  p.setBrush(Qt::NoBrush);
}

}  // namespace

void draw_glyph(QPainter& painter, Glyph kind, const QRectF& b, QColor color) {
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);
  stroke(painter, color, b);
  auto at = [&](double x, double y) {
    return QPointF(b.left() + b.width() * x, b.top() + b.height() * y);
  };
  auto rect = [&](double x1, double y1, double x2, double y2) {
    return QRectF(at(x1, y1), at(x2, y2));
  };
  auto round = [&](double x1, double y1, double x2, double y2, double r) {
    painter.drawRoundedRect(rect(x1, y1, x2, y2), b.width() * r, b.width() * r);
  };
  auto slash = [&] {
    painter.drawLine(at(0.14, 0.14), at(0.86, 0.86));
  };
  switch (kind) {
    case Glyph::Plus:
      round(0.12, 0.12, 0.88, 0.88, 0.16);
      painter.drawLine(at(0.5, 0.29), at(0.5, 0.71));
      painter.drawLine(at(0.29, 0.5), at(0.71, 0.5));
      break;
    case Glyph::Video:
    case Glyph::Camera:
    case Glyph::CameraOff:
      round(0.06, 0.26, 0.66, 0.74, 0.09);
      {
        QPainterPath lens;
        lens.moveTo(at(0.66, 0.39));
        lens.lineTo(at(0.94, 0.27));
        lens.lineTo(at(0.94, 0.73));
        lens.lineTo(at(0.66, 0.61));
        lens.closeSubpath();
        painter.drawPath(lens);
      }
      if (kind == Glyph::CameraOff)
        slash();
      break;
    case Glyph::Calendar:
      round(0.10, 0.18, 0.90, 0.88, 0.09);
      painter.drawLine(at(0.10, 0.36), at(0.90, 0.36));
      painter.drawLine(at(0.30, 0.10), at(0.30, 0.26));
      painter.drawLine(at(0.70, 0.10), at(0.70, 0.26));
      for (double x : {0.29, 0.50, 0.71})
        painter.drawPoint(at(x, 0.60));
      break;
    case Glyph::Gear:
      painter.drawEllipse(rect(0.34, 0.34, 0.66, 0.66));
      for (int i = 0; i < 8; ++i) {
        double a = 2 * 3.141592653589793 * i / 8;
        QPointF c = at(0.5, 0.5);
        QPointF inner = c + QPointF(std::cos(a), std::sin(a)) * b.width() * 0.20;
        QPointF outer = c + QPointF(std::cos(a), std::sin(a)) * b.width() * 0.34;
        painter.drawLine(inner, outer);
      }
      break;
    case Glyph::Mic:
    case Glyph::MicOff:
      round(0.36, 0.10, 0.64, 0.58, 0.14);
      {
        QPainterPath stand;
        stand.moveTo(at(0.22, 0.45));
        stand.quadTo(at(0.50, 0.84), at(0.78, 0.45));
        painter.drawPath(stand);
      }
      painter.drawLine(at(0.50, 0.78), at(0.50, 0.90));
      painter.drawLine(at(0.32, 0.90), at(0.68, 0.90));
      if (kind == Glyph::MicOff)
        slash();
      break;
    case Glyph::Share:
      round(0.08, 0.14, 0.92, 0.70, 0.08);
      painter.drawLine(at(0.30, 0.86), at(0.70, 0.86));
      painter.drawLine(at(0.50, 0.60), at(0.50, 0.32));
      painter.drawLine(at(0.38, 0.44), at(0.50, 0.32));
      painter.drawLine(at(0.62, 0.44), at(0.50, 0.32));
      break;
    case Glyph::Invite:
      painter.drawEllipse(rect(0.32, 0.12, 0.64, 0.44));
      {
        QPainterPath body;
        body.moveTo(at(0.16, 0.84));
        body.quadTo(at(0.48, 0.48), at(0.80, 0.84));
        painter.drawPath(body);
      }
      painter.drawLine(at(0.80, 0.14), at(0.80, 0.42));
      painter.drawLine(at(0.66, 0.28), at(0.94, 0.28));
      break;
    case Glyph::Members:
      painter.drawEllipse(rect(0.08, 0.24, 0.38, 0.54));
      painter.drawEllipse(rect(0.50, 0.16, 0.80, 0.46));
      {
        QPainterPath a;
        a.moveTo(at(0.04, 0.86));
        a.quadTo(at(0.23, 0.56), at(0.44, 0.86));
        painter.drawPath(a);
        QPainterPath c;
        c.moveTo(at(0.46, 0.78));
        c.quadTo(at(0.65, 0.48), at(0.96, 0.78));
        painter.drawPath(c);
      }
      break;
    case Glyph::Chat:
      round(0.08, 0.14, 0.92, 0.72, 0.12);
      {
        QPainterPath tail;
        tail.moveTo(at(0.26, 0.70));
        tail.lineTo(at(0.22, 0.90));
        tail.lineTo(at(0.44, 0.72));
        painter.drawPath(tail);
      }
      for (double x : {0.30, 0.50, 0.70})
        painter.drawPoint(at(x, 0.43));
      break;
    case Glyph::Leave:
      {
        QPainterPath door;
        door.moveTo(at(0.52, 0.14));
        door.lineTo(at(0.28, 0.14));
        door.lineTo(at(0.28, 0.86));
        door.lineTo(at(0.52, 0.86));
        painter.drawPath(door);
      }
      painter.drawLine(at(0.48, 0.50), at(0.90, 0.50));
      painter.drawLine(at(0.74, 0.34), at(0.90, 0.50));
      painter.drawLine(at(0.74, 0.66), at(0.90, 0.50));
      break;
    case Glyph::Layout:
      round(0.12, 0.12, 0.46, 0.46, 0.05);
      round(0.54, 0.12, 0.88, 0.46, 0.05);
      round(0.12, 0.54, 0.46, 0.88, 0.05);
      round(0.54, 0.54, 0.88, 0.88, 0.05);
      break;
    case Glyph::ChevronLeft:
      painter.drawLine(at(0.62, 0.24), at(0.36, 0.50));
      painter.drawLine(at(0.36, 0.50), at(0.62, 0.76));
      break;
    case Glyph::ChevronRight:
      painter.drawLine(at(0.38, 0.24), at(0.64, 0.50));
      painter.drawLine(at(0.64, 0.50), at(0.38, 0.76));
      break;
    case Glyph::Clock:
      painter.drawEllipse(rect(0.12, 0.12, 0.88, 0.88));
      painter.drawLine(at(0.50, 0.28), at(0.50, 0.52));
      painter.drawLine(at(0.50, 0.52), at(0.68, 0.62));
      break;
    case Glyph::Home:
      painter.drawLine(at(0.10, 0.48), at(0.50, 0.14));
      painter.drawLine(at(0.50, 0.14), at(0.90, 0.48));
      painter.drawLine(at(0.20, 0.40), at(0.20, 0.88));
      painter.drawLine(at(0.20, 0.88), at(0.80, 0.88));
      painter.drawLine(at(0.80, 0.88), at(0.80, 0.40));
      break;
    case Glyph::Shield:
      {
        QPainterPath shield;
        shield.moveTo(at(0.50, 0.10));
        shield.lineTo(at(0.84, 0.24));
        shield.lineTo(at(0.80, 0.62));
        shield.quadTo(at(0.68, 0.84), at(0.50, 0.90));
        shield.quadTo(at(0.32, 0.84), at(0.20, 0.62));
        shield.lineTo(at(0.16, 0.24));
        shield.closeSubpath();
        painter.drawPath(shield);
      }
      painter.drawLine(at(0.36, 0.50), at(0.47, 0.61));
      painter.drawLine(at(0.47, 0.61), at(0.66, 0.40));
      break;
  }
  painter.restore();
}

QIcon glyph(Glyph kind, QColor color, int size) {
  // A plain 1x pixmap: QIcon::pixmap()/QLabel::setPixmap() must not receive a
  // devicePixelRatio=2 image, otherwise the widget crops it to the top-left
  // quarter on Windows. Buttons that need crisp scaling draw the glyph directly.
  QPixmap pixmap(size, size);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  draw_glyph(painter, kind, QRectF(0, 0, size, size), color);
  painter.end();
  return QIcon(pixmap);
}

ToolButton::ToolButton(QString text, Glyph icon, QWidget* parent)
    : QPushButton(text, parent), kind_(icon), color_(kOnDark) {
  setProperty("variant", "tool");
  setMouseTracking(true);
  setCursor(Qt::PointingHandCursor);
  setMinimumSize(62, 72);
  setFlat(true);
}

void ToolButton::set_glyph(Glyph kind, QColor color) {
  kind_ = kind;
  if (color.isValid())
    color_ = color;
  update();
}

void ToolButton::set_danger(bool danger) {
  if (danger_ == danger)
    return;
  danger_ = danger;
  update();
}

void ToolButton::set_compact(bool compact) {
  if (compact_ == compact)
    return;
  compact_ = compact;
  setMinimumSize(compact ? 48 : 62, compact ? 56 : 72);
  update();
}

QSize ToolButton::sizeHint() const {
  return compact_ ? QSize(48, 56) : QSize(72, 72);
}

void ToolButton::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  if (!isEnabled())
    p.setOpacity(0.38);
  if (isDown() || isChecked())
    p.fillRect(rect().adjusted(2, 4, -2, 4), QColor(255, 255, 255, 28));
  else if (underMouse() && isEnabled())
    p.fillRect(rect().adjusted(2, 4, -2, 4), QColor(255, 255, 255, 16));
  int side = compact_ ? 26 : 30;
  int top = compact_ ? (height() - side) / 2 : 9;
  draw_glyph(p, kind_, QRectF((width() - side) / 2.0, top, side, side),
             danger_ ? kDanger : color_);
  if (!compact_) {
    QFont font = p.font();
    font.setPixelSize(12);
    p.setFont(font);
    p.setPen(danger_ ? QColor("#ff7a7a") : kOnDark);
    p.drawText(QRect(2, 46, width() - 4, 22), Qt::AlignHCenter | Qt::AlignVCenter, text());
  }
}

ActionCard::ActionCard(QString text, Glyph icon, bool primary, QWidget* parent)
    : QPushButton(text, parent), kind_(icon), primary_(primary) {
  setProperty("variant", "card");
  setMouseTracking(true);
  setCursor(Qt::PointingHandCursor);
  setMinimumHeight(112);
  setFlat(true);
}

QSize ActionCard::sizeHint() const {
  return QSize(220, 112);
}

void ActionCard::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  QRectF card = QRectF(rect()).adjusted(1, 1, -1, -1);
  if (!isEnabled())
    p.setOpacity(0.5);
  QColor fill = primary_ ? (isDown() ? QColor("#0058cc") : kBrand) : QColor("#ffffff");
  if (!primary_ && (underMouse() || isDown()) && isEnabled())
    fill = QColor("#f2f7ff");
  p.setPen(QPen(primary_ ? QColor("#006eff") : kLine, 1));
  p.setBrush(fill);
  p.drawRoundedRect(card, 10, 10);
  QColor ink = primary_ ? QColor("#ffffff") : kInk;
  QColor icon_color = primary_ ? QColor("#ffffff") : kBrand;
  const int side = 40;
  draw_glyph(p, kind_, QRectF((width() - side) / 2.0, 20, side, side), icon_color);
  QFont font = p.font();
  font.setPixelSize(15);
  font.setWeight(QFont::Medium);
  p.setFont(font);
  p.setPen(ink);
  p.drawText(QRect(6, 68, width() - 12, 28), Qt::AlignCenter, text());
}

NavButton::NavButton(QString text, Glyph icon, QString object_name, QWidget* parent)
    : QPushButton(text, parent), kind_(icon) {
  setObjectName(object_name);
  setProperty("variant", "nav");
  setMouseTracking(true);
  setCursor(Qt::PointingHandCursor);
  setFixedSize(64, 62);
  setFlat(true);
}

void NavButton::set_selected(bool selected) {
  selected_ = selected;
  update();
}

QSize NavButton::sizeHint() const {
  return QSize(64, 62);
}

void NavButton::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  QColor ink = selected_ ? kBrand : kSubtle;
  if (selected_)
    p.fillRect(rect().adjusted(0, 6, -6, -6), kBrandLight);
  else if (underMouse())
    p.fillRect(rect().adjusted(0, 6, -6, -6), QColor("#eef1f5"));
  if (selected_) {
    p.setPen(Qt::NoPen);
    p.setBrush(kBrand);
    p.drawRoundedRect(QRectF(0, 12, 3, height() - 24), 1.5, 1.5);
  }
  const int side = 22;
  draw_glyph(p, kind_, QRectF((width() - 6 - side) / 2.0, 11, side, side), ink);
  QFont font = p.font();
  font.setPixelSize(11);
  p.setFont(font);
  p.setPen(ink);
  p.drawText(QRect(0, 38, width() - 6, 18), Qt::AlignCenter, text());
}

QString style_sheet() {
  return QStringLiteral(R"(
QWidget {
  font-family: "Microsoft YaHei UI", "PingFang SC", "Noto Sans CJK SC", sans-serif;
  font-size: 13px;
  color: #1f2329;
}
QMainWindow#conferenceWindow, QDialog { background: #ffffff; }
QWidget#homePage, QWidget#waitingPage { background: #f5f6f8; }
QWidget#meetingPage { background: #17181c; }
QWidget#navRail { background: #ffffff; border-right: 1px solid #e9ebef; }
QWidget#waitingCard { background: #ffffff; border: 1px solid #e9ebef; border-radius: 12px; }
QWidget#listCard { background: #ffffff; border: 1px solid #e9ebef; border-radius: 10px; }
QWidget#meetingTopBar, QWidget#meetingToolbar { background: #1b1c20; }
QWidget#videoStage { background: #101114; }
QWidget#meetingSidePanel, QWidget#meetingPage QTabWidget::pane { background: #22242a; }
QLabel#brand { color: #006eff; font-size: 17px; font-weight: 600; }
QLabel#heading { font-size: 23px; font-weight: 600; color: #1f2329; }
QLabel#sectionTitle { font-size: 15px; font-weight: 600; color: #1f2329; }
QLabel#subtle, QLabel#meetingSubtle, QLabel#meetingNumber, QLabel#memberCount {
  color: #8a9099;
}
QLabel#notice {
  color: #a13b21; background: #fff2e7; padding: 8px 14px;
  border-bottom: 1px solid #f6d8c5;
}
QLabel#meetingTitle { color: #f2f4f8; font-size: 14px; font-weight: 600; }
QLabel#meetingNumber, QLabel#memberCount { color: #8e949f; font-size: 12px; }
QPushButton {
  background: #ffffff; border: 1px solid #e3e6eb; border-radius: 6px;
  padding: 8px 14px; color: #1f2329;
}
QPushButton:hover { background: #f2f7ff; border-color: #a9cbff; color: #006eff; }
QPushButton:pressed { background: #e4efff; }
QPushButton:focus, QLineEdit:focus, QPlainTextEdit:focus, QSpinBox:focus,
QDateTimeEdit:focus, QComboBox:focus { border: 1px solid #006eff; }
QPushButton:disabled { color: #a8adb6; background: #f2f3f5; border-color: #e8eaee; }
QPushButton#primary { background: #006eff; border-color: #006eff; color: #ffffff; }
QPushButton#primary:hover { background: #338bff; border-color: #338bff; color: #ffffff; }
QPushButton#danger { color: #e34b4b; border-color: #f2cccc; }
QPushButton#danger:hover { background: #fff1f1; border-color: #e89a9a; color: #d93b3b; }
QPushButton[variant="tool"], QPushButton[variant="card"], QPushButton[variant="nav"] {
  background: transparent; border: none; padding: 0;
}
QLineEdit, QPlainTextEdit, QSpinBox, QDateTimeEdit, QComboBox {
  background: #ffffff; border: 1px solid #dfe3e9; border-radius: 6px;
  padding: 8px 10px; selection-background-color: #006eff; color: #1f2329;
}
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView {
  background: #ffffff; border: 1px solid #dfe3e9; selection-background-color: #eaf2ff;
  selection-color: #006eff; outline: 0;
}
QListWidget, QTabWidget::pane {
  background: #ffffff; border: 1px solid #e9ebef; border-radius: 8px;
}
QListWidget { padding: 4px; outline: 0; }
QListWidget::item { padding: 12px 10px; border-bottom: 1px solid #f0f2f5; border-radius: 6px; }
QListWidget::item:selected { background: #eaf2ff; color: #006eff; }
QTabWidget::pane { border-top-left-radius: 0; padding: 4px; }
QTabBar::tab {
  padding: 9px 22px; background: transparent; color: #8a9099;
  border-bottom: 2px solid transparent; font-size: 13px;
}
QTabBar::tab:selected { color: #006eff; border-bottom-color: #006eff; }
QCheckBox { spacing: 8px; color: #4b5260; }
QDialog QLabel#heading { font-size: 18px; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }
QScrollBar::handle:vertical { background: #c9ced7; border-radius: 4px; min-height: 24px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QWidget#meetingPage QLabel { color: #e7e9ee; }
QWidget#meetingPage QLabel#meetingSubtle { color: #8e949f; }
QWidget#meetingPage QLabel#meetingTitle { color: #f2f4f8; }
QWidget#meetingPage QLabel#meetingNumber, QWidget#meetingPage QLabel#memberCount {
  color: #8e949f; font-size: 12px;
}
QWidget#meetingPage QComboBox {
  background: #2a2d34; border: 1px solid #3a3e47; color: #e7e9ee; padding: 6px 10px;
}
QWidget#meetingPage QComboBox QAbstractItemView {
  background: #2a2d34; border: 1px solid #3a3e47; color: #e7e9ee;
  selection-background-color: #006eff;
}
QWidget#meetingPage QPlainTextEdit, QWidget#meetingPage QListWidget {
  background: #1b1d22; border: 1px solid #30333b; border-radius: 6px; color: #e7e9ee;
}
QWidget#meetingPage QListWidget::item { border-bottom: 1px solid #30333b; }
QWidget#meetingPage QListWidget::item:selected { background: #2b3f61; color: #ffffff; }
QWidget#meetingPage QTabWidget::pane { background: #22242a; border-color: #30333b; }
QWidget#meetingPage QTabBar::tab { color: #8e949f; border-bottom-color: transparent; }
QWidget#meetingPage QTabBar::tab:selected { color: #ffffff; border-bottom-color: #006eff; }
QWidget#meetingPage QPushButton#sendChat {
  background: #006eff; border-color: #006eff; color: #ffffff; padding: 8px 18px;
}
QWidget#meetingPage QPushButton#previousPage,
QWidget#meetingPage QPushButton#nextPage {
  background: #2a2d34; border: 1px solid #3a3e47; color: #e7e9ee; padding: 4px 10px;
}
QWidget#meetingPage QLabel#pageLabel { color: #aeb4bf; }
)");
}

}  // namespace meeting::ui
