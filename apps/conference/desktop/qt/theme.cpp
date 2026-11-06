#include "theme.hpp"
#include <av/ui.hpp>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <algorithm>

namespace meeting::ui {
namespace {

const QColor kBrand{"#2563eb"};
const QColor kBrandLight{"#eaf2ff"};
const QColor kInk{"#1f2329"};
const QColor kSubtle{"#64748b"};
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
  setAccessibleName(text);
  setToolTip(text);
  setFocusPolicy(Qt::StrongFocus);
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
  if (hasFocus()) {
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor("#93b4ff"), 2));
    p.drawRoundedRect(QRectF(rect()).adjusted(2, 3, -2, -3), 8, 8);
  }
}

ActionCard::ActionCard(QString text, Glyph icon, bool primary, QWidget* parent)
    : QPushButton(text, parent), kind_(icon), primary_(primary) {
  setProperty("variant", "card");
  setAccessibleName(text);
  setToolTip(text);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setCursor(Qt::PointingHandCursor);
  setMinimumHeight(76);
  setFlat(true);
}

QSize ActionCard::sizeHint() const {
  return QSize(220, 76);
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
  p.setPen(QPen(primary_ ? kBrand : kLine, 1));
  p.setBrush(fill);
  p.drawRoundedRect(card, 10, 10);
  QColor ink = primary_ ? QColor("#ffffff") : kInk;
  QColor icon_color = primary_ ? QColor("#ffffff") : kBrand;
  const int side = 30;
  draw_glyph(p, kind_, QRectF(16, (height() - side) / 2.0, side, side), icon_color);
  QFont font = p.font();
  font.setPixelSize(15);
  font.setWeight(QFont::DemiBold);
  p.setFont(font);
  p.setPen(ink);
  p.drawText(QRect(60, 15, width() - 72, 23), Qt::AlignVCenter, text());
  font.setPixelSize(12);
  font.setWeight(QFont::Normal);
  p.setFont(font);
  p.setPen(primary_ ? QColor("#e0eaff") : kSubtle);
  const QString hint = kind_ == Glyph::Plus ? "输入会议号，立即加入"
                         : kind_ == Glyph::Video ? "发起一场即时交流"
                                                : "为下一次讨论安排时间";
  p.drawText(QRect(60, 41, width() - 72, 22), Qt::AlignVCenter, hint);
  if (hasFocus()) {
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(primary_ ? QColor("#ffffff") : kBrand, 2));
    p.drawRoundedRect(card.adjusted(3, 3, -3, -3), 8, 8);
  }
}

NavButton::NavButton(QString text, Glyph icon, QString object_name, QWidget* parent)
    : QPushButton(text, parent), kind_(icon) {
  setObjectName(object_name);
  setProperty("variant", "nav");
  setAccessibleName(text);
  setToolTip(text);
  setFocusPolicy(Qt::StrongFocus);
  setCheckable(true);
  setMouseTracking(true);
  setCursor(Qt::PointingHandCursor);
  setFixedSize(64, 62);
  setFlat(true);
}

void NavButton::set_selected(bool selected) {
  selected_ = selected;
  setChecked(selected);
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
  if (hasFocus()) {
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(kBrand, 2));
    p.drawRoundedRect(QRectF(rect()).adjusted(4, 4, -8, -4), 8, 8);
  }
}

QString style_sheet() {
  return av::ui::style_sheet(av::ui::Theme::Light) + QStringLiteral(R"(
QMainWindow#conferenceWindow, QWidget#homePage, QWidget#waitingPage { background: #f5f7fb; }
QWidget#navRail { background: #ffffff; border-right: 1px solid #e2e8f0; }
QWidget#waitingCard, QWidget#listCard { background: #ffffff; border: 1px solid #e2e8f0; border-radius: 12px; }
QLabel#brand { color: #2563eb; font-size: 16px; font-weight: 600; }
QLabel#heading { color: #0f172a; font-size: 24px; font-weight: 600; }
QLabel#sectionTitle { color: #0f172a; font-size: 16px; font-weight: 600; }
QLabel#subtle, QLabel#meetingSubtle, QLabel#meetingNumber, QLabel#memberCount { color: #64748b; }
QLabel#notice { color: #9a3412; background: #fff7ed; padding: 10px 20px; border-bottom: 1px solid #fed7aa; }
QPushButton[variant="tool"], QPushButton[variant="card"], QPushButton[variant="nav"] { background: transparent; border: none; padding: 0; }
QWidget#meetingPage, QWidget#videoStage { background: #0b1220; }
QWidget#meetingTopBar { background: #ffffff; border-bottom: 1px solid #e2e8f0; }
QLabel#meetingTitle { color: #0f172a; font-size: 14px; font-weight: 600; }
QWidget#meetingToolbar { background: #111c30; border-top: 1px solid #263449; }
QTabWidget#meetingSidePanel { background: #ffffff; border-left: 1px solid #e2e8f0; }
QWidget#meetingPage QPushButton#previousPage, QWidget#meetingPage QPushButton#nextPage {
  background: #1e293b; border: 1px solid #334155; color: #f8fafc; padding: 0;
}
QWidget#meetingPage QLabel#pageLabel { color: #cbd5e1; }
QTabWidget#homeTabs::pane { border: none; }
QListWidget#upcomingMeetings, QListWidget#meetingHistory { border: none; }
QListWidget#upcomingMeetings::item, QListWidget#meetingHistory::item {
  padding: 14px 10px; border-bottom: 1px solid #edf1f7;
}
)");
}

}  // namespace meeting::ui
