#include "window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QLocale>
#include <QLockFile>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/prctl.h>
#include <sys/resource.h>
#endif
int main(int argc, char **argv) {
  umask(0077);
#ifdef __linux__
  struct rlimit limit = {0, 0};
  setrlimit(RLIMIT_CORE, &limit);
  prctl(PR_SET_DUMPABLE, 0);
#endif
  QApplication app(argc, argv);
  app.setApplicationName("QChat");
  app.setOrganizationName("QChat");
  app.setApplicationVersion("0.1.0");
  QCommandLineParser parser;
  parser.setApplicationDescription(
      "Local OpenPGP message helper. No transport.");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption(
      {"smoke-test",
       "Show test UI without creating an identity or touching real app data."});
  parser.addOption({"screenshot",
                    "Save test UI screenshot and exit (requires --smoke-test).",
                    "path"});
  parser.addOption({"language", "UI language: en, de or fa.", "code"});
  parser.process(app);
  const bool smoke = parser.isSet("smoke-test");
  QTemporaryDir temp;
  const auto path = smoke ? temp.path()
                          : QStandardPaths::writableLocation(
                                QStandardPaths::AppLocalDataLocation);
  if (path.isEmpty() || !QDir().mkpath(path))
    return 1;
  QLockFile singleton(path + "/qchat.lock");
  if (!singleton.tryLock()) {
    QMessageBox::warning(
        nullptr, "QChat",
        "QChat is already running, or its data directory is unavailable.");
    return 1;
  }
  QSettings settings(path + "/preferences.ini", QSettings::IniFormat);
  QString language = parser.value("language");
  if (language.isEmpty())
    language =
        settings.value("language", QLocale::system().name().section('_', 0, 0))
            .toString();
  if (language != "de" && language != "fa")
    language = "en";
  QTranslator translator;
  if (language != "en" && translator.load(":/i18n/qchat_" + language + ".qm"))
    app.installTranslator(&translator);
  app.setProperty("language", language);
  app.setLayoutDirection(language == "fa" ? Qt::RightToLeft : Qt::LeftToRight);
  try {
    Crypto::initialize();
    Window window(path, smoke);
    window.show();
    if (smoke) {
      QTimer::singleShot(350, &app, [&] {
        if (parser.isSet("screenshot"))
          window.grab().save(parser.value("screenshot"));
        app.quit();
      });
    }
    return app.exec();
  } catch (const std::exception &) {
    QMessageBox::critical(nullptr, "QChat",
                          "QChat could not initialize. Check that GnuPG and "
                          "GPGME are installed.");
    return 1;
  }
}
