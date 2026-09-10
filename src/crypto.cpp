#include "crypto.h"
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUuid>
#include <gpgme.h>
#include <mutex>
#include <utility>
namespace {
void check(gpgme_error_t e) {
  if (e)
    throw CryptoError(gpgme_strerror(e));
}
struct Context {
  gpgme_ctx_t p = nullptr;
  explicit Context(const QString &home) {
    check(gpgme_new(&p));
    try {
      check(gpgme_set_protocol(p, GPGME_PROTOCOL_OpenPGP));
      check(gpgme_ctx_set_engine_info(p, GPGME_PROTOCOL_OpenPGP, nullptr,
                                      home.toUtf8().constData()));
      check(gpgme_set_ctx_flag(p, "auto-key-retrieve", "0"));
      gpgme_set_armor(p, 1);
      check(gpgme_set_pinentry_mode(p, GPGME_PINENTRY_MODE_ASK));
    } catch (...) {
      gpgme_release(p);
      throw;
    }
  }
  ~Context() { gpgme_release(p); }
};
struct Data {
  gpgme_data_t p = nullptr;
  Data() { check(gpgme_data_new(&p)); }
  explicit Data(const QByteArray &b) {
    check(gpgme_data_new_from_mem(&p, b.constData(), size_t(b.size()), 1));
  }
  ~Data() { gpgme_data_release(p); }
  QByteArray bytes() {
    if (gpgme_data_seek(p, 0, SEEK_SET) < 0)
      throw CryptoError("Cannot read crypto result");
    QByteArray result;
    char b[4096];
    ssize_t n;
    while ((n = gpgme_data_read(p, b, sizeof b)) > 0) {
      result.append(b, int(n));
      if (result.size() > 2 * 1024 * 1024)
        throw CryptoError("Message exceeds size limit");
    }
    if (n < 0)
      throw CryptoError("Cannot read crypto result");
    return result;
  }
};
struct Key {
  gpgme_key_t p = nullptr;
  ~Key() {
    if (p)
      gpgme_key_unref(p);
  }
};
bool usable(gpgme_key_t k) {
  return k && !k->revoked && !k->expired && !k->disabled && !k->invalid &&
         k->can_encrypt;
}
QString fpr(gpgme_key_t k) {
  return k && k->subkeys ? QString::fromLatin1(k->subkeys->fpr) : QString();
}
void limit(const QByteArray &b, int maximum = 1024 * 1024) {
  if (b.isEmpty() || b.size() > maximum)
    throw CryptoError("Input is empty or exceeds the size limit");
}
} // namespace
void Crypto::initialize() {
  static std::once_flag once;
  std::call_once(once, [] {
    if (!gpgme_check_version(nullptr))
      throw CryptoError("GPGME unavailable");
  });
}
Crypto::Crypto(QString home) : home_(std::move(home)) { initialize(); }
void Crypto::prepareHome(const QString &home) {
  if (!QDir().mkpath(home))
    throw CryptoError("Cannot create private application directory");
  if (!QFile::setPermissions(home, QFile::ReadOwner | QFile::WriteOwner |
                                       QFile::ExeOwner))
    throw CryptoError("Cannot restrict directory permissions");
  const auto write = [&](const QString &name, const QByteArray &content) {
    QSaveFile out(home + "/" + name);
    if (!out.open(QIODevice::WriteOnly))
      throw CryptoError("Cannot write crypto configuration");
    out.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    if (out.write(content) != content.size() || !out.commit())
      throw CryptoError("Cannot save crypto configuration");
  };
  write("gpg.conf", "no-auto-key-retrieve\nno-auto-key-import\nauto-key-locate "
                    "clear\nno-symkey-cache\nmax-output 2097152\n");
  write("gpg-agent.conf", "default-cache-ttl 1\nmax-cache-ttl "
                          "1\nno-allow-external-cache\nenforce-passphrase-"
                          "constraints\nmin-passphrase-len 12\n");
}
QVector<Identity> Crypto::keys(bool secret) const {
  Context c(home_);
  check(gpgme_op_keylist_start(c.p, nullptr, secret ? 1 : 0));
  QVector<Identity> out;
  for (;;) {
    Key k;
    auto e = gpgme_op_keylist_next(c.p, &k.p);
    if (gpgme_err_code(e) == GPG_ERR_EOF)
      break;
    check(e);
    out.push_back({fpr(k.p), bool(k.p->secret), usable(k.p)});
  }
  check(gpgme_op_keylist_end(c.p));
  return out;
}
QString Crypto::createIdentity() const {
  Context c(home_);
  check(gpgme_op_createkey(c.p, "QChat identity", "default", 0,
                           365UL * 24 * 60 * 60, nullptr, 0));
  auto r = gpgme_op_genkey_result(c.p);
  if (!r || !r->fpr)
    throw CryptoError("No identity created");
  return QString::fromLatin1(r->fpr);
}
QByteArray Crypto::exportCard(const QString &fingerprint) const {
  Context c(home_);
  Data out;
  check(gpgme_op_export(c.p, fingerprint.toLatin1().constData(), 0, out.p));
  auto b = out.bytes();
  limit(b);
  return b;
}
QString Crypto::importCard(const QByteArray &card) const {
  limit(card);
  if (!card.trimmed().startsWith("-----BEGIN PGP PUBLIC KEY BLOCK-----") ||
      card.contains("PRIVATE KEY") || card.contains("SECRET KEY"))
    throw CryptoError("Only public contact cards are accepted");
  // Parse and re-export only the public certificate before touching the
  // persistent keyring.
  Context preview(home_);
  Data input(card);
  check(gpgme_op_keylist_from_data_start(preview.p, input.p, 0));
  Key k;
  check(gpgme_op_keylist_next(preview.p, &k.p));
  const QString id = fpr(k.p);
  if (!usable(k.p) || k.p->secret || id.isEmpty())
    throw CryptoError("Contact card is expired, revoked or unusable");
  Key extra;
  auto e = gpgme_op_keylist_next(preview.p, &extra.p);
  if (gpgme_err_code(e) != GPG_ERR_EOF)
    throw CryptoError("Exactly one contact card is required");
  check(gpgme_op_keylist_end(preview.p));
  // An untrusted card is first imported into a disposable, isolated keyring.
  QTemporaryDir temporary;
  if (!temporary.isValid())
    throw CryptoError("Cannot validate contact card");
  Crypto::prepareHome(temporary.path());
  Crypto scratch(temporary.path());
  struct Cleanup {
    Crypto &crypto;
    ~Cleanup() { crypto.clearAgent(); }
  } cleanup{scratch};
  {
    Context isolated(temporary.path());
    Data in(card);
    check(gpgme_op_import(isolated.p, in.p));
    auto r = gpgme_op_import_result(isolated.p);
    if (!r || r->secret_read || r->not_imported)
      throw CryptoError("Private material is not a contact card");
  }
  if (!scratch.keys(true).isEmpty())
    throw CryptoError("Private material is not a contact card");
  auto publicOnly = scratch.exportCard(id);
  Context c(home_);
  Data in(publicOnly);
  check(gpgme_op_import(c.p, in.p));
  auto r = gpgme_op_import_result(c.p);
  if (!r || r->not_imported)
    throw CryptoError("Invalid contact card");
  return id;
}
QByteArray Crypto::seal(const QByteArray &text, const QString &recipient,
                        const QString &sender) const {
  limit(text);
  Context c(home_);
  Key r, s;
  check(gpgme_get_key(c.p, recipient.toLatin1().constData(), &r.p, 0));
  check(gpgme_get_key(c.p, sender.toLatin1().constData(), &s.p, 1));
  if (!usable(r.p) || !usable(s.p) || !s.p->can_sign)
    throw CryptoError("Contact or identity cannot be used");
  check(gpgme_signers_add(c.p, s.p));
  gpgme_key_t recipients[] = {r.p, s.p, nullptr};
  Data in(text), out;
  check(gpgme_op_encrypt_sign(c.p, recipients, GPGME_ENCRYPT_ALWAYS_TRUST, in.p,
                              out.p));
  return out.bytes();
}
QByteArray Crypto::encrypt(const QByteArray &text, const QString &recipient) const {
  limit(text);
  Context c(home_);
  Key r;
  check(gpgme_get_key(c.p, recipient.toLatin1().constData(), &r.p, 0));
  if (!usable(r.p))
    throw CryptoError("Recipient cannot be used");
  gpgme_key_t recipients[] = {r.p, nullptr};
  Data in(text), out;
  check(gpgme_op_encrypt(c.p, recipients, GPGME_ENCRYPT_ALWAYS_TRUST, in.p,
                         out.p));
  return out.bytes();
}
QByteArray Crypto::decrypt(const QByteArray &cipher) const {
  limit(cipher, 2 * 1024 * 1024);
  Context c(home_);
  Data in(cipher), out;
  check(gpgme_op_decrypt(c.p, in.p, out.p));
  auto d = gpgme_op_decrypt_result(c.p);
  if (!d || d->legacy_cipher_nomdc)
    throw CryptoError("Unauthenticated legacy encryption rejected");
  return out.bytes();
}
Opened Crypto::open(const QByteArray &cipher) const {
  limit(cipher, 2 * 1024 * 1024);
  Context c(home_);
  Data in(cipher), out;
  // Let GnuPG parse the complete OpenPGP packet.  gpgme_data_identify() is
  // only a heuristic and rejects some valid ASCII-armored messages after they
  // have passed through a text field or an external GnuPG implementation.
  check(gpgme_op_decrypt_verify(c.p, in.p, out.p));
  auto d = gpgme_op_decrypt_result(c.p);
  if (!d || d->legacy_cipher_nomdc)
    throw CryptoError("Unauthenticated legacy encryption rejected");
  auto v = gpgme_op_verify_result(c.p);
  Opened result;
  if (v && v->signatures && !v->signatures->next) {
    auto sig = v->signatures;
    if (!sig->status && sig->fpr &&
        !(sig->summary & (GPGME_SIGSUM_RED | GPGME_SIGSUM_KEY_REVOKED |
                          GPGME_SIGSUM_KEY_EXPIRED | GPGME_SIGSUM_SIG_EXPIRED |
                          GPGME_SIGSUM_SYS_ERROR))) {
      Key k;
      auto e = gpgme_get_key(c.p, sig->fpr, &k.p, 0);
      if (!e && !k.p->revoked && !k.p->expired && !k.p->disabled &&
          !k.p->invalid) {
        result.signer = fpr(k.p);
        result.valid = true;
      }
    }
  }
  // Decryption and sender authentication are separate outcomes. A recipient
  // must be able to read a message encrypted to their public key even when the
  // sender did not attach a signature. The UI marks that plaintext untrusted.
  result.text = out.bytes();
  return result;
}
void Crypto::authenticate(const QString &fingerprint) const {
  Context c(home_);
  Key k;
  check(gpgme_get_key(c.p, fingerprint.toLatin1().constData(), &k.p, 1));
  check(gpgme_signers_add(c.p, k.p));
  Data challenge(QUuid::createUuid().toByteArray()), signature;
  check(gpgme_op_sign(c.p, challenge.p, signature.p, GPGME_SIG_MODE_DETACH));
}
bool Crypto::clearAgent() const {
  QProcess p;
  p.start("gpgconf", {"--homedir", home_, "--kill", "gpg-agent"});
  if (!p.waitForFinished(5000)) {
    p.kill();
    p.waitForFinished();
    return false;
  }
  return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}
