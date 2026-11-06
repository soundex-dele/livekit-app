#include "av/ui.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QImage>
#include <QPushButton>
#include <QScrollArea>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTimer>
#include <QVBoxLayout>
#include <QtTest>

class SharedUiTests : public QObject {
  Q_OBJECT

 private Q_SLOTS:

  void dialog_actions_and_busy() {
    QWidget owner;
    av::ui::apply_theme(&owner, av::ui::Theme::Dark);
    av::ui::Dialog dialog(&owner, QStringLiteral("确认更改"), QStringLiteral("检查设置后保存。"));
    auto* field = new QLineEdit("unchanged", dialog.body());
    dialog.content_layout()->addWidget(field);
    auto* cancel = dialog.add_action(QStringLiteral("取消"));
    auto* save = dialog.add_action(QStringLiteral("保存"), av::ui::ButtonRole::Primary);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    QSignalSpy rejected(&dialog, &QDialog::rejected);
    dialog.show();
    QCoreApplication::processEvents();
    QCOMPARE(av::ui::theme_for(field), av::ui::Theme::Dark);
    dialog.set_busy(true);
    QTest::keyClick(&dialog, Qt::Key_Escape);
    QVERIFY(dialog.isVisible());
    QVERIFY(!save->isEnabled());
    QCOMPARE(rejected.count(), 0);
    dialog.set_busy(false);
    dialog.set_error(QStringLiteral("保存失败，请重试。输入内容已保留。"));
    QCOMPARE(field->text(), QStringLiteral("unchanged"));
    QVERIFY(dialog.findChild<QLabel*>("avDialogError")->isVisible());
    QTest::mouseClick(save, Qt::LeftButton);
    QCOMPARE(accepted.count(), 1);
    QCOMPARE(rejected.count(), 0);
  }

  void safe_message_default() {
    bool found = false;
    QTimer::singleShot(0, [&found] {
      auto* dialog = QApplication::activeModalWidget();
      const auto close = qScopeGuard([dialog] {
        if (auto* modal = qobject_cast<QDialog*>(dialog)) {
          modal->reject();
        }
      });
      QVERIFY(dialog);
      auto* cancel = dialog->findChild<QPushButton*>("avDialogAction65536");
      QVERIFY(cancel);
      QVERIFY(cancel->isDefault());
      found = true;
      QTest::keyClick(dialog, Qt::Key_Return);
    });
    const auto result = av::ui::MessageBox::question(nullptr, QStringLiteral("结束会话"),
                                                     QStringLiteral("结束后将断开连接。"),
                                                     QMessageBox::Yes | QMessageBox::No);
    QVERIFY(found);
    QCOMPARE(result, QMessageBox::No);
  }

  void escape_never_confirms() {
    QTimer::singleShot(0, [] {
      auto* dialog = QApplication::activeModalWidget();
      const auto close = qScopeGuard([dialog] {
        if (auto* modal = qobject_cast<QDialog*>(dialog)) {
          modal->reject();
        }
      });
      QVERIFY(dialog);
      QTest::keyClick(dialog, Qt::Key_Escape);
    });
    QCOMPARE(av::ui::MessageBox::warning(nullptr, "Delete", "Delete this item?",
                                         QMessageBox::Yes | QMessageBox::Cancel),
             QMessageBox::Cancel);
    bool accepted = true;
    QTimer::singleShot(0, [] {
      auto* dialog = QApplication::activeModalWidget();
      const auto close = qScopeGuard([dialog] {
        if (auto* modal = qobject_cast<QDialog*>(dialog)) {
          modal->reject();
        }
      });
      QVERIFY(dialog);
      QTest::keyClick(dialog, Qt::Key_Escape);
    });
    const auto text = av::ui::InputDialog::getText(nullptr, "Name", "Name", QLineEdit::Normal,
                                                   "existing", &accepted);
    QVERIFY(!accepted);
    QVERIFY(text.isEmpty());
  }

  void gallery_and_long_content() {
    const QString directory = qEnvironmentVariable("AV_UI_SCREENSHOT_DIR",
                                                   QDir::currentPath() + "/screenshots/shared-ui");
    QVERIFY(QDir().mkpath(directory));
    for (auto theme : {av::ui::Theme::Light, av::ui::Theme::Dark}) {
      QWidget gallery;
      av::ui::apply_theme(&gallery, theme);
      gallery.resize(720, 760);
      auto* layout = new QVBoxLayout(&gallery);
      layout->setContentsMargins(24, 24, 24, 24);
      layout->setSpacing(16);
      auto* title = new QLabel(QStringLiteral("AV · 通用组件"), &gallery);
      av::ui::set_text_role(title, "title");
      layout->addWidget(title);
      auto* row = new QHBoxLayout;
      for (auto role : {av::ui::ButtonRole::Primary, av::ui::ButtonRole::Secondary,
                        av::ui::ButtonRole::Danger, av::ui::ButtonRole::Ghost}) {
        auto* button = new QPushButton(QStringLiteral("操作按钮"), &gallery);
        av::ui::set_button_role(button, role);
        row->addWidget(button);
      }
      layout->addLayout(row);
      auto* field = new QLineEdit(&gallery);
      field->setPlaceholderText(QStringLiteral("输入连接码或会议号"));
      layout->addWidget(field);
      auto* choice = new QComboBox(&gallery);
      choice->addItems({QStringLiteral("默认设备"), QStringLiteral("其他设备")});
      layout->addWidget(choice);
      layout->addWidget(new QCheckBox(QStringLiteral("记住本次设置"), &gallery));
      for (auto tone : {av::ui::Tone::Info, av::ui::Tone::Success, av::ui::Tone::Warning,
                        av::ui::Tone::Error}) {
        layout->addWidget(new av::ui::Notice(
            QStringLiteral("状态提示：完整说明当前状态与下一步操作。"), tone, &gallery));
      }
      layout->addWidget(new av::ui::EmptyState(
          QStringLiteral("准备就绪"), QStringLiteral("选择文件或连接设备，即可开始。"), &gallery));
      gallery.show();
      QCoreApplication::processEvents();
      const QString mode = theme == av::ui::Theme::Dark ? "dark" : "light";
      const QString scale = qEnvironmentVariable("QT_SCALE_FACTOR", "1");
      QVERIFY(gallery.grab().save(directory + "/components-" + mode + "-" + scale + "x.png"));
      av::ui::Dialog dialog(&gallery, QStringLiteral("连接设置与长内容"),
                            QStringLiteral("内容可以滚动，底部操作始终可见。"));
      for (int i = 0; i < 20; ++i) {
        auto* label = new QLabel(
            QStringLiteral("第 %1 项：长中文说明保留完整内容，不裁断操作按钮。").arg(i + 1));
        label->setWordWrap(true);
        dialog.content_layout()->addWidget(label);
      }
      auto* cancel = dialog.add_action(QStringLiteral("取消"));
      auto* confirm = dialog.add_action(QStringLiteral("保存设置"), av::ui::ButtonRole::Primary);
      dialog.show();
      QCoreApplication::processEvents();
      auto* scroll = dialog.findChild<QScrollArea*>("avDialogScroll");
      QVERIFY(scroll);
      QVERIFY(cancel->isVisible());
      QVERIFY(confirm->isVisible());
      const QRect footer = dialog.findChild<QWidget*>("avDialogFooter")->geometry();
      QVERIFY(dialog.rect().contains(footer));
      QVERIFY(footer.top() >= scroll->geometry().bottom());
      QVERIFY(dialog.grab().save(directory + "/dialog-" + mode + "-" + scale + "x.png"));
      dialog.reject();
    }
  }
};

QTEST_MAIN(SharedUiTests)
#include "ui_tests.moc"
