#include "crypto.h"
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include <gpgme.h>
class CryptoTest : public QObject {
  Q_OBJECT
  QTemporaryDir aliceDir, bobDir, malloryDir;
  QString a, b, m;
  QByteArray run(const QString &home, QStringList args,
                 const QByteArray &input = {}) {
    QProcess p;
    QStringList all{"--homedir",       home,       "--batch",      "--yes",
                    "--pinentry-mode", "loopback", "--passphrase", ""};
    all += args;
    p.start("gpg", all);
    if (!p.waitForStarted())
      throw CryptoError("test gpg start");
    p.write(input);
    p.closeWriteChannel();
    if (!p.waitForFinished(30000) || p.exitCode() != 0)
      throw CryptoError(p.readAllStandardError().constData());
    return p.readAllStandardOutput();
  }
  QString fixture(const QString &home, const QString &name) {
    Crypto::prepareHome(home); // Test fixtures deliberately use unprotected
                               // throwaway identities.
    run(home, {"--quick-generate-key", name, "default", "default", "1d"});
    return Crypto(home).keys(true).first().fingerprint;
  }
private slots:
  void initTestCase() {
    a = fixture(aliceDir.path(), "Test Alice");
    b = fixture(bobDir.path(), "Test Bob");
    m = fixture(malloryDir.path(), "Test Mallory");
    Crypto alice(aliceDir.path()), bob(bobDir.path());
    QCOMPARE(alice.importCard(bob.exportCard(b)), b);
    QCOMPARE(bob.importCard(alice.exportCard(a)), a);
  }
  void protectedIdentitySetup() {
    QTemporaryDir temporary;
    Crypto::prepareHome(temporary.path());
    // A test-only Assuan pinentry. Never used by the application.
    QFile pinentry(temporary.path() + "/test-pinentry");
    QVERIFY(pinentry.open(QIODevice::WriteOnly));
    pinentry.write("#!/bin/sh\nprintf 'OK test pinentry\\n'\nwhile IFS= read "
                   "-r line; do\ncase \"$line\" in\nGETPIN*) printf 'D "
                   "QChat-test-passphrase-1234\\nOK\\n';;\nBYE*) printf "
                   "'OK\\n'; exit 0;;\n*) printf 'OK\\n';;\nesac\ndone\n");
    pinentry.close();
    QVERIFY(pinentry.setPermissions(QFile::ReadOwner | QFile::WriteOwner |
                                    QFile::ExeOwner));
    QFile config(temporary.path() + "/gpg-agent.conf");
    QVERIFY(config.open(QIODevice::Append));
    config.write("pinentry-program " + pinentry.fileName().toUtf8() + "\n");
    config.close();
    Crypto crypto(temporary.path());
    struct Stop {
      Crypto &crypto;
      ~Stop() { crypto.clearAgent(); }
    } stop{crypto};
    auto identity = crypto.createIdentity();
    QVERIFY(!identity.isEmpty());
    const QDir secretDir(temporary.path() + "/private-keys-v1.d");
    const auto entries = secretDir.entryList({"*.key"}, QDir::Files);
    QVERIFY(entries.size() >= 2);
    for (const auto &name : entries) {
      QFile key(secretDir.filePath(name));
      QVERIFY(key.open(QIODevice::ReadOnly));
      QVERIFY(key.readAll().contains("protected-private-key"));
    }
    QVERIFY(crypto.clearAgent());
    crypto.authenticate(identity);
  }
  void unicodeRoundTrip() {
    Crypto alice(aliceDir.path()), bob(bobDir.path());
    auto text = QString::fromUtf8("Hallo — سلام دنیا 🌍\nSecond line").toUtf8();
    auto encrypted = alice.seal(text, b, a);
    QVERIFY(encrypted.startsWith("-----BEGIN PGP MESSAGE-----"));
    auto opened = bob.open(encrypted);
    QVERIFY(opened.valid);
    QCOMPARE(opened.signer, a);
    QCOMPARE(opened.text, text);
    auto ownCopy = alice.open(encrypted);
    QCOMPARE(ownCopy.text, text);
  }
  void wrongRecipient() {
    Crypto alice(aliceDir.path()), mallory(malloryDir.path());
    auto encrypted = alice.seal("private", b, a);
    QVERIFY_EXCEPTION_THROWN(mallory.open(encrypted), CryptoError);
  }
  void corruptedCiphertext() {
    Crypto alice(aliceDir.path()), bob(bobDir.path());
    auto encrypted = alice.seal("private", b, a);
    encrypted = encrypted.left(encrypted.size() / 2);
    QVERIFY_EXCEPTION_THROWN(bob.open(encrypted), CryptoError);
  }
  void unsignedNotDisplayed() {
    auto encrypted = run(
        aliceDir.path(),
        {"--armor", "--trust-model", "always", "--encrypt", "--recipient", b},
        "unsigned");
    auto opened = Crypto(bobDir.path()).open(encrypted);
    QVERIFY(!opened.valid);
    QCOMPARE(opened.text, QByteArray("unsigned"));
  }
  void unknownSignerNotDisplayed() {
    Crypto mallory(malloryDir.path()), bob(bobDir.path());
    mallory.importCard(bob.exportCard(b));
    auto encrypted = mallory.seal("unknown sender", b, m);
    auto opened = bob.open(encrypted);
    QVERIFY(!opened.valid);
    QCOMPARE(opened.text, QByteArray("unknown sender"));
  }
  void signedOnlyRejected() {
    auto signedText =
        run(aliceDir.path(), {"--armor", "--clearsign"}, "not encrypted");
    QVERIFY_EXCEPTION_THROWN(Crypto(bobDir.path()).open(signedText),
                             CryptoError);
  }
  void externallyArmoredCiphertextAccepted() {
    Crypto alice(aliceDir.path()), bob(bobDir.path());
    const auto text = QByteArray("Hallo world");
    const auto ciphertext = run(
        aliceDir.path(),
        {"--armor", "--trust-model", "always", "--encrypt", "--recipient",
         b},
        text);
    const auto opened = bob.open(ciphertext);
    QVERIFY(!opened.valid);
    QCOMPARE(opened.text, text);
  }
  void malformedCard() {
    QVERIFY_EXCEPTION_THROWN(Crypto(bobDir.path()).importCard("not a card"),
                             CryptoError);
  }
  void multipleCardsRejected() {
    auto cards = Crypto(aliceDir.path()).exportCard(a) +
                 Crypto(malloryDir.path()).exportCard(m);
    QVERIFY_EXCEPTION_THROWN(Crypto(bobDir.path()).importCard(cards),
                             CryptoError);
  }
  void secretImportRejected() {
    auto secret =
        run(malloryDir.path(), {"--armor", "--export-secret-keys", m});
    QVERIFY_EXCEPTION_THROWN(Crypto(bobDir.path()).importCard(secret),
                             CryptoError);
    secret.replace("PRIVATE KEY BLOCK", "PUBLIC KEY BLOCK");
    QVERIFY_EXCEPTION_THROWN(Crypto(bobDir.path()).importCard(secret),
                             CryptoError);
    QCOMPARE(Crypto(bobDir.path()).keys(true).size(), 1);
  }
  void maximumMessageRoundTrip() {
    QByteArray text(1024 * 1024, Qt::Uninitialized);
    QRandomGenerator random(12345);
    for (auto &byte : text)
      byte = char(random.generate() & 255);
    Crypto alice(aliceDir.path()), bob(bobDir.path());
    const auto cipher = alice.seal(text, b, a);
    QVERIFY(cipher.size() > text.size());
    try {
      QCOMPARE(bob.open(cipher).text, text);
    } catch (const CryptoError &e) {
      QFAIL(e.what());
    }
  }
  void oversizedInputRejected() {
    QVERIFY_EXCEPTION_THROWN(
        Crypto(aliceDir.path()).seal(QByteArray(1024 * 1024 + 1, 'x'), b, a),
        CryptoError);
  }
  void cleanupTestCase() {
    Crypto(aliceDir.path()).clearAgent();
    Crypto(bobDir.path()).clearAgent();
    Crypto(malloryDir.path()).clearAgent();
  }
};
QTEST_GUILESS_MAIN(CryptoTest)
#include "crypto_test.moc"
