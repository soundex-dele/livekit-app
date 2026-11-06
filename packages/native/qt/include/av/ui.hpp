#pragma once

#include <QColor>
#include <QDialog>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QStringList>

class QAbstractButton;
class QHBoxLayout;
class QPushButton;
class QShowEvent;
class QVBoxLayout;

namespace av::ui {

enum class Theme { Light, Dark };
enum class ButtonRole { Secondary, Primary, Danger, Ghost };
enum class Tone { Neutral, Info, Success, Warning, Error };
enum class Icon {
  Plus,
  Close,
  Check,
  Info,
  Warning,
  Error,
  Settings,
  Home,
  Video,
  Mic,
  Screen,
  Play,
  Pause,
  Stop,
  Folder,
  ChevronRight,
  Copy,
  Users,
  Link
};

// UI-thread-only helpers. Widgets remain owned by their Qt parent; themes are
// scoped to a widget subtree. No application-wide palette or media state changes.
// Native Windows title bars follow the top-level widget's theme without forcing
// native handle creation. Child-widget themes do not recolor their owner window.
QString style_sheet(Theme theme);
void apply_theme(QWidget* root, Theme theme);
Theme theme_for(const QWidget* widget);
void set_button_role(QAbstractButton* button, ButtonRole role);
void set_surface(QWidget* widget, const char* role = "card");
void set_text_role(QLabel* label, const char* role);
void set_tone(QWidget* widget, Tone tone);
QIcon icon(Icon kind, QColor color, int size = 20);

class Card : public QFrame {
 public:
  explicit Card(QWidget* parent = nullptr);
};

class StatusBadge : public QLabel {
 public:
  explicit StatusBadge(const QString& text, QWidget* parent = nullptr);
  void set_status(const QString& text, Tone tone);
};

class Notice : public QLabel {
 public:
  Notice(const QString& text, Tone tone, QWidget* parent = nullptr);
  void set_message(const QString& text, Tone tone);
};

class EmptyState : public QWidget {
 public:
  EmptyState(const QString& title, const QString& description, QWidget* parent = nullptr);
  void set_text(const QString& title, const QString& description);

 private:
  QLabel* title_;
  QLabel* description_;
};

// A themed, window-modal dialog with a bounded, scrollable body and fixed
// action area. add_action does not connect accept/reject: the application owns
// validation and asynchronous completion. Busy dialogs reject no user input.
class Dialog : public QDialog {
 public:
  explicit Dialog(QWidget* parent, const QString& title = {}, const QString& description = {});
  QVBoxLayout* content_layout() const;
  QHBoxLayout* button_layout() const;
  QWidget* body() const;
  QPushButton* add_action(const QString& text, ButtonRole role = ButtonRole::Secondary);
  void set_error(const QString& text);
  void set_busy(bool busy);
  void reject() override;
  void done(int result) override;

 protected:
  void showEvent(QShowEvent* event) override;

 private:
  QWidget* body_;
  QWidget* footer_;
  QVBoxLayout* content_;
  QHBoxLayout* actions_;
  Notice* error_;
  QPointer<QWidget> opener_;
  bool busy_ = false;
};

// Compatibility-shaped facades backed by Dialog, not platform message boxes.
// A dismissed question always returns Cancel/No, never the affirmative action.
class MessageBox {
 public:
  static QMessageBox::StandardButton question(
      QWidget* parent, const QString& title, const QString& text,
      QMessageBox::StandardButtons buttons = QMessageBox::Yes | QMessageBox::No,
      QMessageBox::StandardButton default_button = QMessageBox::NoButton);
  static QMessageBox::StandardButton information(
      QWidget* parent, const QString& title, const QString& text,
      QMessageBox::StandardButtons buttons = QMessageBox::Ok,
      QMessageBox::StandardButton default_button = QMessageBox::NoButton);
  static QMessageBox::StandardButton warning(
      QWidget* parent, const QString& title, const QString& text,
      QMessageBox::StandardButtons buttons = QMessageBox::Ok,
      QMessageBox::StandardButton default_button = QMessageBox::NoButton);
  static QMessageBox::StandardButton critical(
      QWidget* parent, const QString& title, const QString& text,
      QMessageBox::StandardButtons buttons = QMessageBox::Ok,
      QMessageBox::StandardButton default_button = QMessageBox::NoButton);
};

class InputDialog {
 public:
  static QString getText(QWidget* parent, const QString& title, const QString& label,
                         QLineEdit::EchoMode mode = QLineEdit::Normal, const QString& text = {},
                         bool* ok = nullptr, Qt::WindowFlags flags = {},
                         Qt::InputMethodHints hints = Qt::ImhNone);
  static QString getItem(QWidget* parent, const QString& title, const QString& label,
                         const QStringList& items, int current = 0, bool editable = true,
                         bool* ok = nullptr, Qt::WindowFlags flags = {},
                         Qt::InputMethodHints hints = Qt::ImhNone);
};

QColor choose_color(const QColor& initial, QWidget* parent, const QString& title = {});

}  // namespace av::ui
