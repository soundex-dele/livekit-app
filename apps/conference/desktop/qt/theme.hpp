#pragma once
#include <QColor>
#include <QIcon>
#include <QPushButton>

class QPainter;
class QRectF;

namespace meeting::ui {

// All artwork is drawn at runtime with QPainter. No third-party logo, icon set
// or screenshot is bundled with the client.
enum class Glyph {
  Plus,
  Video,
  Calendar,
  Gear,
  Mic,
  MicOff,
  Camera,
  CameraOff,
  Share,
  Invite,
  Members,
  Chat,
  Leave,
  Layout,
  ChevronLeft,
  ChevronRight,
  Clock,
  Home,
  Shield,
};

void draw_glyph(QPainter& painter, Glyph kind, const QRectF& box, QColor color);
QIcon glyph(Glyph kind, QColor color, int size = 32);
QString style_sheet();

// Icon above label, the arrangement used by the meeting toolbar and by the
// desktop client's home cards. Derives from QPushButton so existing
// findChild<QPushButton*> lookups keep working.
class ToolButton : public QPushButton {
 public:
  ToolButton(QString text, Glyph icon, QWidget* parent = nullptr);
  void set_glyph(Glyph kind, QColor color = {});
  void set_danger(bool danger);
  void set_compact(bool compact);
  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent*) override;

 private:
  Glyph kind_;
  QColor color_;
  bool danger_ = false;
  bool compact_ = false;
};

class ActionCard : public QPushButton {
 public:
  ActionCard(QString text, Glyph icon, bool primary, QWidget* parent = nullptr);
  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent*) override;

 private:
  Glyph kind_;
  bool primary_;
};

class NavButton : public QPushButton {
 public:
  NavButton(QString text, Glyph icon, QString object_name, QWidget* parent = nullptr);
  void set_selected(bool selected);
  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent*) override;

 private:
  Glyph kind_;
  bool selected_ = false;
};

}  // namespace meeting::ui
