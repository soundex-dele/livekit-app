#pragma once

#include <QtGlobal>

class QWidget;

namespace av::ui::detail {

// Follows this widget's theme when it owns a native top-level window. Does not
// create a window handle or change the surrounding window for themed children.
void install_window_theme(QWidget* widget);

// DWM caption/text/border colors are write-only. Hidden-window integration tests
// observe real platform calls and their results; no platform calls are replaced.
using WindowThemeObserver = void (*)(const QWidget*, quint32 attribute, quint32 value,
                                     qint32 result);
void set_window_theme_observer_for_testing(WindowThemeObserver observer);

}  // namespace av::ui::detail
