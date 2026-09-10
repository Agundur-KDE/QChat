#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>
#include <stdexcept>

struct Identity {
  QString fingerprint;
  bool secret = false;
  bool usable = false;
};
struct Opened {
  QByteArray text;
  QString signer;
  bool valid = false;
};
class CryptoError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};
class Crypto {
public:
  explicit Crypto(QString home);
  static void initialize();
  static void prepareHome(const QString &home);
  QVector<Identity> keys(bool secret = false) const;
  QString createIdentity() const;
  QByteArray exportCard(const QString &fingerprint) const;
  QString importCard(const QByteArray &card) const;
  QByteArray seal(const QByteArray &text, const QString &recipient,
                  const QString &sender) const;
  Opened open(const QByteArray &cipher) const;
  void authenticate(const QString &fingerprint) const;
  bool clearAgent() const;
  QString home() const { return home_; }

private:
  QString home_;
};
