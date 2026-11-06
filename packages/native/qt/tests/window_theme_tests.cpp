#include "av/ui.hpp"
#include "../src/window_theme.hpp"

#include <QApplication>
#include <QEvent>
#include <QHash>
#include <QtTest>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>

namespace {

constexpr DWORD kImmersiveDarkMode = 20;
constexpr DWORD kBorderColor = 34;
constexpr DWORD kCaptionColor = 35;
constexpr DWORD kTextColor = 36;

struct AttributeWrite {
  quint32 value = 0;
  qint32 result = 0;
};

QHash<const QWidget*, QHash<quint32, AttributeWrite>> attribute_writes;

void observe_attribute_write(const QWidget* widget, quint32 attribute, quint32 value,
                             qint32 result) {
  attribute_writes[widget][attribute] = {value, result};
}

void verify_color_write(const QHash<quint32, AttributeWrite>& writes, DWORD attribute,
                        COLORREF expected) {
  const auto found = writes.constFind(attribute);
  QVERIFY(found != writes.cend());
  QCOMPARE(found->value, static_cast<quint32>(expected));
  QCOMPARE(found->result, static_cast<qint32>(S_OK));
}

// These tests create native handles without showing or activating any window.
// QWidget::grab() under the offscreen platform cannot inspect DWM decorations.
class HiddenWindow : public QWidget {
 public:
  explicit HiddenWindow(QWidget* parent = nullptr) : QWidget(parent) {
    setAttribute(Qt::WA_DontShowOnScreen);
  }

  void destroy_native_window() {
    destroy();
  }
};

void verify_theme(QWidget& window, av::ui::Theme theme) {
  window.ensurePolished();
  QCoreApplication::processEvents();
  const auto handle = reinterpret_cast<HWND>(window.internalWinId());
  QVERIFY(handle);
  QVERIFY(IsWindow(handle));
  QVERIFY(!window.isVisible());
  QVERIFY(!IsWindowVisible(handle));

  const bool dark = theme == av::ui::Theme::Dark;
  BOOL dark_mode = FALSE;
  QCOMPARE(DwmGetWindowAttribute(handle, kImmersiveDarkMode, &dark_mode, sizeof(dark_mode)), S_OK);
  QCOMPARE(dark_mode, dark ? TRUE : FALSE);

  // DWM exposes these colors through Set only. The observer records the real
  // native calls and HRESULTs; it does not replace or simulate the platform API.
  const auto writes = attribute_writes.constFind(&window);
  QVERIFY(writes != attribute_writes.cend());
  verify_color_write(*writes, kCaptionColor, dark ? RGB(0x15, 0x1F, 0x30) : RGB(0xFF, 0xFF, 0xFF));
  verify_color_write(*writes, kTextColor, dark ? RGB(0xE7, 0xED, 0xF7) : RGB(0x17, 0x24, 0x3B));
  verify_color_write(*writes, kBorderColor, dark ? RGB(0x34, 0x43, 0x5A) : RGB(0xD5, 0xDE, 0xEB));
}

}  // namespace

class WindowThemeTests : public QObject {
  Q_OBJECT

 private Q_SLOTS:

  void initTestCase() {
    if (QGuiApplication::platformName() != QLatin1String("windows")) {
      QSKIP("Native caption checks require the Windows platform, not offscreen rendering.");
    }
    HIGHCONTRASTW high_contrast{};
    high_contrast.cbSize = sizeof(high_contrast);
    QVERIFY(SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(high_contrast), &high_contrast, 0));
    if (high_contrast.dwFlags & HCF_HIGHCONTRASTON) {
      QSKIP("System high contrast owns native caption colors.");
    }

    HiddenWindow probe;
    const auto handle = reinterpret_cast<HWND>(probe.winId());
    QVERIFY(handle);
    QVERIFY(!IsWindowVisible(handle));
    const COLORREF requested_caption = RGB(0xFF, 0xFF, 0xFF);
    const auto result =
        DwmSetWindowAttribute(handle, kCaptionColor, &requested_caption, sizeof(requested_caption));
    if (result == E_INVALIDARG) {
      QSKIP(
          "This Windows version does not expose custom DWM caption colors (Windows 11 required).");
    }
    QCOMPARE(result, S_OK);
    av::ui::detail::set_window_theme_observer_for_testing(observe_attribute_write);
  }

  void cleanup() {
    attribute_writes.clear();
  }

  void cleanupTestCase() {
    av::ui::detail::set_window_theme_observer_for_testing(nullptr);
    attribute_writes.clear();
  }

  void theme_before_native_handle() {
    for (auto theme : {av::ui::Theme::Light, av::ui::Theme::Dark}) {
      HiddenWindow window;
      QVERIFY(!window.internalWinId());
      av::ui::apply_theme(&window, theme);
      // Styling a future child widget must not create a native top-level window.
      QVERIFY(!window.internalWinId());
      QVERIFY(window.winId());
      verify_theme(window, theme);
    }
  }

  void theme_after_native_handle_and_switches() {
    HiddenWindow window;
    const auto handle = window.winId();
    QVERIFY(handle);
    for (auto theme : {av::ui::Theme::Light, av::ui::Theme::Dark, av::ui::Theme::Light}) {
      av::ui::apply_theme(&window, theme);
      QCOMPARE(window.internalWinId(), handle);
      verify_theme(window, theme);
    }
  }

  void dialog_inherits_owner_theme() {
    for (auto theme : {av::ui::Theme::Light, av::ui::Theme::Dark}) {
      HiddenWindow owner;
      av::ui::apply_theme(&owner, theme);
      av::ui::Dialog dialog(&owner, QStringLiteral("Window theme"));
      dialog.setAttribute(Qt::WA_DontShowOnScreen);
      QVERIFY(dialog.winId());
      verify_theme(dialog, theme);
      QVERIFY(!owner.isVisible());
      if (owner.internalWinId()) {
        verify_theme(owner, theme);
      }
    }
  }

  void child_theme_preserves_owner_caption() {
    HiddenWindow owner;
    av::ui::apply_theme(&owner, av::ui::Theme::Light);
    QVERIFY(owner.winId());

    // Conference media surfaces receive their theme before joining the layout.
    HiddenWindow media;
    av::ui::apply_theme(&media, av::ui::Theme::Dark);
    QVERIFY(!media.internalWinId());
    media.setParent(&owner);
    QVERIFY(!media.isWindow());
    QVERIFY(media.winId());
    QCoreApplication::processEvents();
    verify_theme(owner, av::ui::Theme::Light);
    QVERIFY(!IsWindowVisible(reinterpret_cast<HWND>(media.internalWinId())));

    av::ui::apply_theme(&media, av::ui::Theme::Dark);
    verify_theme(owner, av::ui::Theme::Light);
  }

  void child_promoted_to_window() {
    HiddenWindow owner;
    av::ui::apply_theme(&owner, av::ui::Theme::Light);
    HiddenWindow controller(&owner);
    av::ui::apply_theme(&controller, av::ui::Theme::Dark);
    controller.setWindowFlag(Qt::Window, true);
    QVERIFY(controller.isWindow());
    QVERIFY(controller.winId());
    verify_theme(controller, av::ui::Theme::Dark);
  }

  void native_handle_recreation() {
    HiddenWindow window;
    av::ui::apply_theme(&window, av::ui::Theme::Light);
    QVERIFY(window.winId());
    verify_theme(window, av::ui::Theme::Light);

    window.setWindowFlag(Qt::WindowStaysOnTopHint, true);
    QVERIFY(window.winId());
    verify_theme(window, av::ui::Theme::Light);
    attribute_writes.remove(&window);
    window.destroy_native_window();
    QVERIFY(!window.internalWinId());
    QVERIFY(window.winId());
    verify_theme(window, av::ui::Theme::Light);
  }

  void theme_event_restores_native_colors() {
    HiddenWindow window;
    av::ui::apply_theme(&window, av::ui::Theme::Light);
    const auto handle = reinterpret_cast<HWND>(window.winId());
    verify_theme(window, av::ui::Theme::Light);

    // Simulate platform decoration colors being replaced during a theme change.
    const BOOL dark_mode = TRUE;
    const COLORREF black = RGB(0, 0, 0);
    QCOMPARE(DwmSetWindowAttribute(handle, kImmersiveDarkMode, &dark_mode, sizeof(dark_mode)),
             S_OK);
    for (auto attribute : {kCaptionColor, kTextColor, kBorderColor}) {
      QCOMPARE(DwmSetWindowAttribute(handle, attribute, &black, sizeof(black)), S_OK);
    }
    attribute_writes.remove(&window);
    QEvent event(QEvent::ThemeChange);
    QCoreApplication::sendEvent(&window, &event);
    verify_theme(window, av::ui::Theme::Light);
  }
};

QTEST_MAIN(WindowThemeTests)
#include "window_theme_tests.moc"
