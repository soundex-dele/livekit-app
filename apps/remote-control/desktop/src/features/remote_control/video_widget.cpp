#include "video_widget.hpp"
#include <QFocusEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <livekit/remote_control.hpp>
#ifdef __APPLE__
#include <map>
#endif
namespace remote_app {
QRectF fit_video(QRectF b, QSize s) {
  if (s.isEmpty() || b.isEmpty())
    return {};
  auto z = QSizeF(s);
  z.scale(b.size(), Qt::KeepAspectRatio);
  return {b.center() - QPointF(z.width() / 2, z.height() / 2), z};
}

std::optional<QPointF> video_point(QRectF b, QSize source, QPointF p) {
  auto r = fit_video(b, source);
  if (r.isEmpty() || !r.contains(p))
    return {};
  return QPointF(std::clamp((p.x() - r.x()) / r.width(), 0.0, 1.0),
                 std::clamp((p.y() - r.y()) / r.height(), 0.0, 1.0));
}

std::string key_code(const QKeyEvent& e) {
#ifdef Q_OS_WIN
  const unsigned scan = e.nativeScanCode();
  static const auto known = []() {
    std::vector<std::string> keys;
    for (char c = 'A'; c <= 'Z'; ++c)
      keys.push_back(std::string("Key") + c);
    for (char c = '0'; c <= '9'; ++c) {
      keys.push_back(std::string("Digit") + c);
      keys.push_back(std::string("Numpad") + c);
    }
    for (int i = 1; i <= 12; ++i)
      keys.push_back("F" + std::to_string(i));
    for (auto k :
         {"Escape",       "Minus",         "Equal",        "Backspace",      "Tab",
          "BracketLeft",  "BracketRight",  "Enter",        "ControlLeft",    "Semicolon",
          "Quote",        "Backquote",     "ShiftLeft",    "Backslash",      "Comma",
          "Period",       "Slash",         "ShiftRight",   "NumpadMultiply", "AltLeft",
          "Space",        "CapsLock",      "NumLock",      "ScrollLock",     "NumpadSubtract",
          "NumpadAdd",    "NumpadDecimal", "ControlRight", "AltRight",       "NumpadEnter",
          "NumpadDivide", "Home",          "ArrowUp",      "PageUp",         "ArrowLeft",
          "ArrowRight",   "End",           "ArrowDown",    "PageDown",       "Insert",
          "Delete",       "MetaLeft",      "MetaRight",    "ContextMenu"})
      keys.emplace_back(k);
    return keys;
  }();
  if (scan)
    for (const auto& k : known)
      if (livekit::sdk::control_key_scan_code(k) == scan)
        return k;
#elif defined(Q_OS_MACOS)
  // Cocoa's nativeVirtualKey is the physical ANSI/ISO keycode, independent of
  // layout.
  static const std::map<unsigned, std::string> native = {{0, "KeyA"},
                                                         {1, "KeyS"},
                                                         {2, "KeyD"},
                                                         {3, "KeyF"},
                                                         {4, "KeyH"},
                                                         {5, "KeyG"},
                                                         {6, "KeyZ"},
                                                         {7, "KeyX"},
                                                         {8, "KeyC"},
                                                         {9, "KeyV"},
                                                         {11, "KeyB"},
                                                         {12, "KeyQ"},
                                                         {13, "KeyW"},
                                                         {14, "KeyE"},
                                                         {15, "KeyR"},
                                                         {16, "KeyY"},
                                                         {17, "KeyT"},
                                                         {18, "Digit1"},
                                                         {19, "Digit2"},
                                                         {20, "Digit3"},
                                                         {21, "Digit4"},
                                                         {22, "Digit6"},
                                                         {23, "Digit5"},
                                                         {24, "Equal"},
                                                         {25, "Digit9"},
                                                         {26, "Digit7"},
                                                         {27, "Minus"},
                                                         {28, "Digit8"},
                                                         {29, "Digit0"},
                                                         {30, "BracketRight"},
                                                         {31, "KeyO"},
                                                         {32, "KeyU"},
                                                         {33, "BracketLeft"},
                                                         {34, "KeyI"},
                                                         {35, "KeyP"},
                                                         {36, "Enter"},
                                                         {37, "KeyL"},
                                                         {38, "KeyJ"},
                                                         {39, "Quote"},
                                                         {40, "KeyK"},
                                                         {41, "Semicolon"},
                                                         {42, "Backslash"},
                                                         {43, "Comma"},
                                                         {44, "Slash"},
                                                         {45, "KeyN"},
                                                         {46, "KeyM"},
                                                         {47, "Period"},
                                                         {48, "Tab"},
                                                         {49, "Space"},
                                                         {50, "Backquote"},
                                                         {51, "Backspace"},
                                                         {53, "Escape"},
                                                         {54, "MetaRight"},
                                                         {55, "MetaLeft"},
                                                         {56, "ShiftLeft"},
                                                         {57, "CapsLock"},
                                                         {58, "AltLeft"},
                                                         {59, "ControlLeft"},
                                                         {60, "ShiftRight"},
                                                         {61, "AltRight"},
                                                         {62, "ControlRight"},
                                                         {65, "NumpadDecimal"},
                                                         {67, "NumpadMultiply"},
                                                         {69, "NumpadAdd"},
                                                         {71, "NumLock"},
                                                         {75, "NumpadDivide"},
                                                         {76, "NumpadEnter"},
                                                         {78, "NumpadSubtract"},
                                                         {82, "Numpad0"},
                                                         {83, "Numpad1"},
                                                         {84, "Numpad2"},
                                                         {85, "Numpad3"},
                                                         {86, "Numpad4"},
                                                         {87, "Numpad5"},
                                                         {88, "Numpad6"},
                                                         {89, "Numpad7"},
                                                         {91, "Numpad8"},
                                                         {92, "Numpad9"},
                                                         {122, "F1"},
                                                         {120, "F2"},
                                                         {99, "F3"},
                                                         {118, "F4"},
                                                         {96, "F5"},
                                                         {97, "F6"},
                                                         {98, "F7"},
                                                         {100, "F8"},
                                                         {101, "F9"},
                                                         {109, "F10"},
                                                         {103, "F11"},
                                                         {111, "F12"},
                                                         {115, "Home"},
                                                         {116, "PageUp"},
                                                         {117, "Delete"},
                                                         {119, "End"},
                                                         {121, "PageDown"},
                                                         {123, "ArrowLeft"},
                                                         {124, "ArrowRight"},
                                                         {125, "ArrowDown"},
                                                         {126, "ArrowUp"}};
  if (e.spontaneous() || e.nativeVirtualKey() != 0) {
    auto it = native.find(e.nativeVirtualKey());
    if (it != native.end())
      return it->second;
  }
#endif
  if (e.key() >= Qt::Key_A && e.key() <= Qt::Key_Z)
    return std::string("Key") + char('A' + e.key() - Qt::Key_A);
  if (e.key() >= Qt::Key_0 && e.key() <= Qt::Key_9)
    return std::string(e.modifiers().testFlag(Qt::KeypadModifier) ? "Numpad" : "Digit") +
           char('0' + e.key() - Qt::Key_0);
  if (e.key() >= Qt::Key_F1 && e.key() <= Qt::Key_F12)
    return "F" + std::to_string(e.key() - Qt::Key_F1 + 1);
  switch (e.key()) {
    case Qt::Key_Control:
      return "ControlLeft";
    case Qt::Key_Meta:
      return "MetaLeft";
    case Qt::Key_Alt:
      return "AltLeft";
    case Qt::Key_Shift:
      return "ShiftLeft";
    case Qt::Key_Return:
      return "Enter";
    case Qt::Key_Enter:
      return "NumpadEnter";
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
      return "Tab";
    case Qt::Key_Backspace:
      return "Backspace";
    case Qt::Key_Delete:
      return "Delete";
    case Qt::Key_Escape:
      return "Escape";
    case Qt::Key_Space:
      return "Space";
    case Qt::Key_Left:
      return "ArrowLeft";
    case Qt::Key_Right:
      return "ArrowRight";
    case Qt::Key_Up:
      return "ArrowUp";
    case Qt::Key_Down:
      return "ArrowDown";
    case Qt::Key_Home:
      return "Home";
    case Qt::Key_End:
      return "End";
    case Qt::Key_PageUp:
      return "PageUp";
    case Qt::Key_PageDown:
      return "PageDown";
    default:
      return {};
  }
}

VideoWidget::VideoWidget(QWidget* p) : QWidget(p) {
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setMinimumSize(320, 180);
  setAccessibleName("远程桌面，点击启用键鼠，离开窗口暂停输入");
}

void VideoWidget::present(QImage image, bool available, bool busy, uint64_t layout) {
  if (layout_ != layout || !available) {
    pause();
    layout_ = layout;
  }
  image_ = std::move(image);
  available_ = available;
  busy_ = busy;
  if (original_ && !image_.isNull())
    resize(image_.size() / devicePixelRatioF());
  update();
}

void VideoWidget::pause() {
  enabled_ = false;
  target_ = false;
  if (released)
    released();
  update();
}

void VideoWidget::set_original(bool v) {
  pause();
  original_ = v;
  setMinimumSize(v && !image_.isNull() ? image_.size() / devicePixelRatioF() : QSize(320, 180));
  if (v && !image_.isNull())
    resize(image_.size() / devicePixelRatioF());
  updateGeometry();
  update();
}

std::optional<QPointF> VideoWidget::point(QPointF p) const {
  return video_point(rect(), image_.size(), p);
}

void VideoWidget::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), QColor("#172033"));
  if (image_.isNull()) {
    p.setPen(QColor("#cbd5e1"));
    p.drawText(rect(), Qt::AlignCenter, "等待远程画面…");
  } else {
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(fit_video(rect(), image_.size()), image_);
  }
  if (hasFocus()) {
    p.setPen(QPen(QColor("#60a5fa"), 2));
    p.drawRect(rect().adjusted(1, 1, -2, -2));
  }
}

void VideoWidget::mouse(QMouseEvent* e, bool down) {
  auto p = point(e->position());
  if (!available_ || busy_ || !p) {
    if (!down)
      pause();
    return;
  }
  int b = e->button() == Qt::LeftButton     ? 0
          : e->button() == Qt::MiddleButton ? 1
          : e->button() == Qt::RightButton  ? 2
                                            : -1;
  if (b < 0)
    return;
  if (down) {
    setFocus();
    enabled_ = target_ = true;
    if (target_selected)
      target_selected();
  }
  if (enabled_ && send)
    send({{"type", "button"}, {"button", b}, {"down", down}, {"x", p->x()}, {"y", p->y()}});
  e->accept();
}

void VideoWidget::mousePressEvent(QMouseEvent* e) {
  mouse(e, true);
}

void VideoWidget::mouseReleaseEvent(QMouseEvent* e) {
  mouse(e, false);
}

void VideoWidget::mouseMoveEvent(QMouseEvent* e) {
  auto p = point(e->position());
  if (enabled_ && !busy_ && p && send)
    send({{"type", "move"}, {"x", p->x()}, {"y", p->y()}});
}

void VideoWidget::wheelEvent(QWheelEvent* e) {
  auto p = point(e->position());
  if (enabled_ && !busy_ && p && send) {
    int d = e->angleDelta().y();
    if (!d && e->pixelDelta().y())
      d = e->pixelDelta().y() > 0 ? 120 : -120;
    d = std::clamp(d, -1200, 1200);
    send({{"type", "wheel"}, {"delta", d}, {"x", p->x()}, {"y", p->y()}});
    e->accept();
  }
}

void VideoWidget::keyPressEvent(QKeyEvent* e) {
  auto code = key_code(*e);
  if (enabled_ && !busy_ && !code.empty() && send) {
    send({{"type", "key"}, {"code", code}, {"down", true}});
    e->accept();
  } else
    QWidget::keyPressEvent(e);
}

void VideoWidget::keyReleaseEvent(QKeyEvent* e) {
  if (e->isAutoRepeat())
    return;
  auto code = key_code(*e);
  if (enabled_ && !busy_ && !code.empty() && send) {
    send({{"type", "key"}, {"code", code}, {"down", false}});
    e->accept();
  } else
    QWidget::keyReleaseEvent(e);
}

void VideoWidget::focusOutEvent(QFocusEvent* e) {
  enabled_ = false;
  if (released)
    released();
  QWidget::focusOutEvent(e);
}

bool VideoWidget::event(QEvent* e) {
  if (e->type() == QEvent::ShortcutOverride && enabled_) {
    e->accept();
    return true;
  }
  if (e->type() == QEvent::KeyPress && enabled_ &&
      (static_cast<QKeyEvent*>(e)->key() == Qt::Key_Tab ||
       static_cast<QKeyEvent*>(e)->key() == Qt::Key_Backtab)) {
    keyPressEvent(static_cast<QKeyEvent*>(e));
    return true;
  }
  return QWidget::event(e);
}
}  // namespace remote_app
