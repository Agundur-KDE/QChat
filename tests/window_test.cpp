#include "window.h"
#include <QTemporaryDir>
#include <QTranslator>
#include <QtTest>
#include <QtWidgets>
class WindowTest : public QObject {
  Q_OBJECT
  QTemporaryDir aliceDir, bobDir;
  QString a, b;
  std::unique_ptr<Window> window;
  void generate(const QString &home, const QString &name) {
    Crypto::prepareHome(home);
    QProcess p;
    p.start("gpg", {"--homedir", home, "--batch", "--pinentry-mode", "loopback",
                    "--passphrase", "", "--quick-generate-key", name, "default",
                    "default", "1d"});
    if (!p.waitForFinished(30000) || p.exitCode() != 0)
      throw CryptoError(p.readAllStandardError().constData());
  }
private slots:
  void initTestCase() {
    QCoreApplication::setApplicationName("QChatTest");
    generate(aliceDir.path() + "/gnupg", "UI Alice");
    generate(bobDir.path() + "/gnupg", "UI Bob");
    Crypto alice(aliceDir.path() + "/gnupg"), bob(bobDir.path() + "/gnupg");
    a = alice.keys(true).first().fingerprint;
    b = bob.keys(true).first().fingerprint;
    alice.importCard(bob.exportCard(b));
    bob.importCard(alice.exportCard(a));
  }
  void init() {
    window = std::make_unique<Window>(aliceDir.path());
    window->show();
    QTRY_VERIFY_WITH_TIMEOUT(!window->busy_, 10000);
    QCOMPARE(window->own_, a);
    window->unlock();
    QTRY_VERIFY_WITH_TIMEOUT(!window->busy_, 10000);
    QVERIFY(!window->locked_);
    window->contacts_ = {{"Bob", b, true}};
    window->refreshContacts();
  }
  void unverifiedCannotSend() {
    window->contacts_[0].verified = false;
    window->refreshContacts();
    QVERIFY(!window->sealButton_->isEnabled());
    window->draft_->setPlainText("must not send");
    window->seal();
    QVERIFY(window->sealed_->toPlainText().isEmpty());
  }
  void sendAndClearStaleOutput() {
    window->draft_->setPlainText("Hallo سلام");
    window->seal();
    QTRY_VERIFY_WITH_TIMEOUT(!window->busy_, 10000);
    QVERIFY(window->draft_->toPlainText().isEmpty());
    QVERIFY(window->copyButton_->isEnabled());
    auto out = Crypto(bobDir.path() + "/gnupg")
                   .open(window->sealed_->toPlainText().toUtf8());
    QCOMPARE(QString::fromUtf8(out.text), QString("Hallo سلام"));
    window->draft_->setPlainText("changed");
    QVERIFY(window->sealed_->toPlainText().isEmpty());
    QVERIFY(!window->copyButton_->isEnabled());
  }
  void receiveAndLock() {
    auto msg = Crypto(bobDir.path() + "/gnupg").seal("Private hello", a, b);
    window->incoming_->setPlainText(QString::fromUtf8(msg));
    window->openMessage();
    QTRY_VERIFY_WITH_TIMEOUT(!window->busy_, 10000);
    QCOMPARE(window->plain_->toPlainText(), QString("Private hello"));
    window->lock();
    QVERIFY(window->plain_->toPlainText().isEmpty());
    QVERIFY(window->incoming_->toPlainText().isEmpty());
    QVERIFY(window->contacts_.isEmpty());
    QCOMPARE(window->pages_->currentIndex(), 0);
    QVERIFY(!QFile::exists(aliceDir.path() + "/history.json"));
  }
  void unverifiedSenderHidden() {
    window->contacts_[0].verified = false;
    auto msg = Crypto(bobDir.path() + "/gnupg").seal("must not appear", a, b);
    window->incoming_->setPlainText(QString::fromUtf8(msg));
    window->openMessage();
    QTRY_VERIFY_WITH_TIMEOUT(!window->busy_, 10000);
    QCOMPARE(window->plain_->toPlainText(), QString("must not appear"));
    QVERIFY(window->senderInfo_->text().contains("not been checked"));
  }
  void pastedCiphertextNormalization() {
    const auto message = Crypto(bobDir.path() + "/gnupg").seal("normalized", a, b);
    QString indented = QString::fromUtf8(message);
    auto lines = indented.split('\n');
    for (auto &line : lines)
      line.prepend("   ");
    const auto wrapped = QString("\ufeff```text\r\n") +
                         lines.join("\r\n") + "\r\n```\r\n";
    QCOMPARE(Window::normalizeCiphertext(wrapped), message.trimmed());
  }
  void lockDiscardsPendingResult() {
    bool callback = false;
    window->job(
        [] {
          QThread::msleep(150);
          TaskResult r;
          r.bytes = "late plaintext";
          return r;
        },
        [&](TaskResult) { callback = true; });
    window->lock();
    QTRY_VERIFY_WITH_TIMEOUT(!window->busy_, 10000);
    QVERIFY(!callback);
    QVERIFY(window->locked_);
  }
  void persistentContactsReloadUnverified() {
    window->contacts_[0].verified = false;
    QVERIFY(window->saveContacts());
    window->contacts_.clear();
    window->loadContacts();
    QCOMPARE(window->contacts_.size(), 1);
    QVERIFY(!window->contacts_[0].verified);
  }
  void languageLayouts() {
    for (const QString language : {QString("de"), QString("fa")}) {
      QTranslator translator;
      QVERIFY(translator.load(QCoreApplication::applicationDirPath() +
                              "/qchat_" + language + ".qm"));
      qApp->installTranslator(&translator);
      qApp->setProperty("language", language);
      qApp->setLayoutDirection(language == "fa" ? Qt::RightToLeft
                                                : Qt::LeftToRight);
      Window preview(aliceDir.path(), true);
      preview.pages_->setCurrentIndex(1);
      preview.contacts_ = {{QString::fromUtf8("دوست / Kontakt"), b, true}};
      preview.locked_ = false;
      preview.refreshContacts();
      preview.draft_->setPlainText(
          QString::fromUtf8("سلام!\nDies ist eine Testnachricht."));
      preview.show();
      QTest::qWait(50);
      QCOMPARE(preview.layoutDirection(),
               language == "fa" ? Qt::RightToLeft : Qt::LeftToRight);
      QVERIFY(preview.sealButton_->text() != "Encrypt message");
      auto artifacts = qEnvironmentVariable("QCHAT_TEST_ARTIFACTS");
      if (!artifacts.isEmpty()) {
        QDir().mkpath(artifacts);
        QVERIFY(preview.grab().save(artifacts + "/qchat-" + language + ".png"));
      }
      qApp->removeTranslator(&translator);
    }
    qApp->setLayoutDirection(Qt::LeftToRight);
  }
  void cleanup() { window.reset(); }
  void cleanupTestCase() {
    Crypto(aliceDir.path() + "/gnupg").clearAgent();
    Crypto(bobDir.path() + "/gnupg").clearAgent();
  }
};
QTEST_MAIN(WindowTest)
#include "window_test.moc"
