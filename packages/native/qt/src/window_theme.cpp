#include "window_theme.hpp"

#include "av/ui.hpp"

#include <QApplication>
#include <QDebug>
#include <QEvent>
#include <QTimer>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

namespace av::ui::detail {
namespace {

WindowThemeObserver window_theme_observer = nullptr;

#ifdef Q_OS_WIN
// Numeric values keep builds compatible with older Windows SDK headers.
// Exact caption colors are available on Windows 11; Windows 10 still uses the
// matching light/dark native caption through the immersive-dark-mode attribute.
constexpr DWORD kImmersiveDarkMode = 20;
constexpr DWORD kLegacyImmersiveDarkMode = 19;
constexpr DWORD kBorderColor = 34;
constexpr DWORD kCaptionColor = 35;
constexpr DWORD kTextColor = 36;
constexpr COLORREF kDefaultColor = 0xFFFFFFFF;

COLORREF native_color(const QColor& color) {
  return RGB(color.red(), color.green(), color.blue());
}

HRESULT set_attribute(QWidget* widget, HWND handle, DWORD attribute, DWORD value) {
  const HRESULT result = DwmSetWindowAttribute(handle, attribute, &value, sizeof(value));
  if (window_theme_observer) {
    window_theme_observer(widget, attribute, value, result);
  }
  return result;
}

void report_attribute_failure(DWORD attribute, HRESULT result) {
  // Older Windows versions do not support every decoration attribute.
  if (FAILED(result) && result != E_INVALIDARG) {
    qWarning().nospace() << "Window theme attribute " << attribute << " failed: 0x" << Qt::hex
                         << static_cast<quint32>(result);
  }
}

void apply_native_theme(QWidget* widget) {
  if (!widget->isWindow() || widget->windowType() == Qt::Popup ||
      widget->windowType() == Qt::ToolTip || widget->windowType() == Qt::Desktop ||
      widget->windowFlags().testFlag(Qt::FramelessWindowHint)) {
    return;
  }
  // winId() would create a HWND during construction, including for media panels
  // that have not yet been parented. Only decorate an already-created window.
  const auto handle = reinterpret_cast<HWND>(widget->internalWinId());
  if (!handle || !IsWindow(handle)) {
    return;
  }
  HIGHCONTRASTW contrast{};
  contrast.cbSize = sizeof(contrast);
  const bool high_contrast =
      SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
      (contrast.dwFlags & HCF_HIGHCONTRASTON);
  const bool dark = theme_for(widget) == Theme::Dark;
  const BOOL native_dark = dark && !high_contrast;
  HRESULT result = set_attribute(widget, handle, kImmersiveDarkMode, native_dark);
  if (result == E_INVALIDARG) {
    result = set_attribute(widget, handle, kLegacyImmersiveDarkMode, native_dark);
  }
  report_attribute_failure(kImmersiveDarkMode, result);

  // QSS polishing can replace palette roles with a widget's background, or
  // retain the previous palette while changing themes. Native decorations must
  // use the theme's surface/text tokens rather than that mutable palette.
  const COLORREF caption =
      high_contrast ? kDefaultColor : native_color(QColor(dark ? "#151F30" : "#FFFFFF"));
  const COLORREF text =
      high_contrast ? kDefaultColor : native_color(QColor(dark ? "#E7EDF7" : "#17243B"));
  const COLORREF border =
      high_contrast ? kDefaultColor : native_color(QColor(dark ? "#34435A" : "#D5DEEB"));
  report_attribute_failure(kCaptionColor, set_attribute(widget, handle, kCaptionColor, caption));
  report_attribute_failure(kTextColor, set_attribute(widget, handle, kTextColor, text));
  report_attribute_failure(kBorderColor, set_attribute(widget, handle, kBorderColor, border));
}

class WindowThemeFilter final : public QObject {
 public:
  explicit WindowThemeFilter(QWidget* widget) : QObject(widget), widget_(widget) {
    setObjectName(QStringLiteral("_avWindowTheme"));
    widget_->installEventFilter(this);
  }

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    switch (event->type()) {
      case QEvent::WinIdChange:
      case QEvent::Show:
      case QEvent::ParentChange:
      case QEvent::ThemeChange:
      case QEvent::PaletteChange:
      case QEvent::ApplicationPaletteChange:
      case QEvent::StyleChange:
      case QEvent::WindowActivate:
      case QEvent::WindowDeactivate:
        apply_native_theme(widget_);
        if (!update_pending_) {
          update_pending_ = true;
          // Qt may refresh native decorations while processing the same event.
          // Reapply afterwards; using this context cancels work on destruction.
          QTimer::singleShot(0, this, [this] {
            update_pending_ = false;
            apply_native_theme(widget_);
          });
        }
        break;
      default:
        break;
    }
    return QObject::eventFilter(watched, event);
  }

 private:
  QWidget* widget_;
  bool update_pending_ = false;
};
#endif

}  // namespace

void install_window_theme(QWidget* widget) {
#ifdef Q_OS_WIN
  if (!widget || QGuiApplication::platformName() != QLatin1String("windows")) {
    return;
  }
  if (!widget->findChild<QObject*>(QStringLiteral("_avWindowTheme"), Qt::FindDirectChildrenOnly)) {
    new WindowThemeFilter(widget);
  }
  apply_native_theme(widget);
#else
  Q_UNUSED(widget);
#endif
}

void set_window_theme_observer_for_testing(WindowThemeObserver observer) {
  window_theme_observer = observer;
}

}  // namespace av::ui::detail
