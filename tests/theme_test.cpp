#include "window.h"
#include <QtTest>
#include <QtWidgets>
#include <cmath>

static double luminance(const QColor &color) {
  auto linear = [](double c) {
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
  };
  return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) +
         .0722 * linear(color.blueF());
}
static double contrast(QColor a, QColor b) {
  double x = luminance(a), y = luminance(b);
  return (qMax(x, y) + .05) / (qMin(x, y) + .05);
}
class ThemeTest : public QObject {
  Q_OBJECT
private slots:
  void explicitThemesAreIndependentOfSystemPalette() {
    QTemporaryDir temporary;
    Window preview(temporary.path(), true);
    preview.applyTheme("dark");
    QCOMPARE(qApp->palette().color(QPalette::Window), QColor("#202426"));
    QCOMPARE(qApp->palette().color(QPalette::Base), QColor("#171b1d"));
    preview.applyTheme("light");
    QCOMPARE(qApp->palette().color(QPalette::Window), QColor("#f4f6f8"));
    QCOMPARE(qApp->palette().color(QPalette::Base), QColor("#ffffff"));
  }
  void readableControls_data() {
    QTest::addColumn<bool>("dark");
    QTest::newRow("light") << false;
    QTest::newRow("dark") << true;
  }
  void readableControls() {
    QFETCH(bool, dark);
    const auto original = qApp->palette();
    struct Restore {
      QPalette palette;
      ~Restore() { qApp->setPalette(palette); }
    } restore{original};
    QPalette palette;
    QColor surface = dark ? QColor("#202426") : QColor("#f4f6f8");
    QColor field = dark ? QColor("#171b1d") : QColor("#ffffff");
    QColor text = dark ? QColor("#f4f6f8") : QColor("#17283b");
    for (auto group :
         {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
      palette.setColor(group, QPalette::Window, surface);
      palette.setColor(group, QPalette::Base, field);
      palette.setColor(group, QPalette::Button, surface);
      for (auto role :
           {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        palette.setColor(group, role, text);
      palette.setColor(group, QPalette::Highlight, QColor("#225b88"));
      palette.setColor(group, QPalette::HighlightedText, Qt::white);
    }
    qApp->setPalette(palette);
    QTemporaryDir temporary;
    Window preview(temporary.path(), true);
    preview.show();
    QMessageBox dialog(QMessageBox::Information, "Language",
                       "The language changes next time QChat starts.",
                       QMessageBox::Ok, &preview);
    dialog.show();
    QTest::qWait(30);
    const auto labels = dialog.findChildren<QLabel *>();
    bool found = false;
    for (auto *label : labels) {
      if (label->text().isEmpty())
        continue;
      found = true;
      QVERIFY2(contrast(label->palette().color(QPalette::WindowText),
                        dialog.palette().color(QPalette::Window)) >= 4.5,
               "Dialog text must contrast with the actual dialog background");
    }
    QVERIFY(found);
    auto artifacts = qEnvironmentVariable("QCHAT_TEST_ARTIFACTS");
    if (!artifacts.isEmpty()) {
      QDir().mkpath(artifacts);
      QVERIFY(dialog.grab().save(
          artifacts + QString("/dialog-%1.png").arg(dark ? "dark" : "light")));
    }
    dialog.close();
    auto *combo = preview.findChild<QComboBox *>();
    QVERIFY(combo);
    combo->showPopup();
    QTest::qWait(30);
    const auto popup = combo->view()->palette();
    QVERIFY2(contrast(popup.color(QPalette::Text),
                      popup.color(QPalette::Base)) >= 4.5,
             "Popup items must contrast with the popup background");
    QVERIFY(contrast(popup.color(QPalette::HighlightedText),
                     popup.color(QPalette::Highlight)) >= 4.5);
    if (!artifacts.isEmpty())
      QVERIFY(combo->view()->window()->grab().save(
          artifacts + QString("/popup-%1.png").arg(dark ? "dark" : "light")));
    combo->hidePopup();
  }
};
QTEST_MAIN(ThemeTest)
#include "theme_test.moc"
