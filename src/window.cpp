#include "window.h"
#include "systemwatch.h"
#include <QCryptographicHash>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtConcurrent>
#include <QtWidgets>
namespace {
QLabel *label(const QString &text) {
  auto *l = new QLabel(text);
  l->setWordWrap(true);
  l->setTextFormat(Qt::PlainText);
  return l;
}
QPushButton *button(const QString &text) {
  auto *b = new QPushButton(text);
  b->setMinimumHeight(36);
  return b;
}
QPlainTextEdit *editor(const QString &hint, bool readOnly = false) {
  auto *e = new QPlainTextEdit;
  e->setPlaceholderText(hint);
  e->setReadOnly(readOnly);
  e->setUndoRedoEnabled(false);
  e->setAcceptDrops(false);
  return e;
}
constexpr int maxInput = 1024 * 1024;
} // namespace
Window::Window(QString path, bool smoke)
    : crypto_(path + "/gnupg"), path_(std::move(path)), smoke_(smoke) {
  systemPalette_ = qApp->palette();
  setWindowTitle(tr("QChat — private message helper"));
  resize(920, 720);
  setMinimumSize(680, 540);
  build();
  sessionTimer_.setSingleShot(true);
  sessionTimer_.setInterval(3 * 60 * 1000);
  connect(&sessionTimer_, &QTimer::timeout, this, &Window::lock);
  clipboardTimer_.setSingleShot(true);
  clipboardTimer_.setInterval(45000);
  connect(&clipboardTimer_, &QTimer::timeout, this, &Window::clearClipboard);
  auto *shortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  connect(shortcut, &QShortcut::activated, this, &Window::lock);
  connect(qApp, &QGuiApplication::applicationStateChanged, this,
          [this](Qt::ApplicationState state) {
            if (state == Qt::ApplicationSuspended)
              lock();
          });
  if (!smoke_) {
    auto *watch = new SystemWatch(this);
    connect(watch, &SystemWatch::protectionRequested, this, &Window::lock);
    if (!watch->usbAvailable())
      status_->setText(
          tr("USB monitoring is unavailable. The timed app lock still works."));
  }
  if (smoke_) {
    gateText_->setText(tr("Create your private identity on this computer. "
                          "Choose a strong passphrase in the separate GnuPG "
                          "window. You do not need an email address."));
    gateButton_->setText(tr("Set up QChat"));
  } else
    discover();
}
Window::~Window() {
  clearClipboard();
  if (!smoke_)
    crypto_.clearAgent();
}
QString Window::code(const QString &f) {
  QStringList groups;
  for (int i = 0; i < f.size(); i += 4)
    groups << f.mid(i, 4);
  return groups.join(' ');
}
void Window::build() {
  auto *root = new QWidget;
  auto *layout = new QVBoxLayout(root);
  layout->setContentsMargins(24, 20, 24, 16);
  layout->setSpacing(14);
  setCentralWidget(root);
  auto *top = new QHBoxLayout;
  auto *title = new QLabel("QChat");
  title->setObjectName("title");
  top->addWidget(title);
  top->addStretch();
  languageBox_ = new QComboBox;
  languageBox_->addItem("English", "en");
  languageBox_->addItem("Deutsch", "de");
  languageBox_->addItem(QString::fromUtf8("فارسی"), "fa");
  const auto lang = qApp->property("language").toString();
  languageBox_->setCurrentIndex(qMax(0, languageBox_->findData(lang)));
  top->addWidget(languageBox_);
  themeBox_ = new QComboBox;
  themeBox_->addItem(tr("System"), "system");
  themeBox_->addItem(tr("Light"), "light");
  themeBox_->addItem(tr("Dark"), "dark");
  const auto savedTheme = QSettings(path_ + "/preferences.ini", QSettings::IniFormat)
                              .value("theme", "system").toString();
  themeBox_->setCurrentIndex(qMax(0, themeBox_->findData(savedTheme)));
  top->addWidget(themeBox_);
  auto *helpButton = button(tr("Safety & help"));
  top->addWidget(helpButton);
  lockButton_ = button(tr("Lock now · Esc"));
  top->addWidget(lockButton_);
  layout->addLayout(top);
  connect(helpButton, &QPushButton::clicked, this, &Window::help);
  connect(lockButton_, &QPushButton::clicked, this, &Window::lock);
  connect(languageBox_, &QComboBox::currentIndexChanged, this, [this] {
    QSettings settings(path_ + "/preferences.ini", QSettings::IniFormat);
    settings.setValue("language", languageBox_->currentData());
    QMessageBox::information(
        this, tr("Language"),
        tr("The language changes next time QChat starts. Persian is a draft "
           "translation and needs native-speaker review."));
  });
  connect(themeBox_, &QComboBox::currentIndexChanged, this,
          [this](int index) {
            if (index < 0)
              return;
            const auto theme = themeBox_->itemData(index).toString();
            QSettings settings(path_ + "/preferences.ini", QSettings::IniFormat);
            settings.setValue("theme", theme);
            applyTheme(theme);
          });
  applyTheme(themeBox_->currentData().toString());
  layout->addWidget(label(
      tr("Encrypt here. Send elsewhere. No account, no message history.")));
  pages_ = new QStackedWidget;
  layout->addWidget(pages_, 1);
  auto *gate = new QWidget;
  auto *gl = new QVBoxLayout(gate);
  gl->addStretch();
  auto *gt = label(tr("Your messages stay under your control"));
  gt->setObjectName("heading");
  gl->addWidget(gt);
  gateText_ = label(tr("Checking local setup…"));
  gl->addWidget(gateText_);
  gateButton_ = button(tr("Unlock"));
  gl->addWidget(gateButton_);
  gl->addWidget(
      label(tr("An app lock is not disk encryption. Public contact cards "
               "remain on this computer. No secure-deletion guarantee.")));
  gl->addStretch();
  pages_->addWidget(gate);
  connect(gateButton_, &QPushButton::clicked, this, &Window::unlock);
  work_ = new QWidget;
  auto *wl = new QVBoxLayout(work_);
  wl->setContentsMargins(0, 0, 0, 0);
  auto *contactRow = new QHBoxLayout;
  contactsBox_ = new QComboBox;
  contactsBox_->setMinimumWidth(200);
  contactRow->addWidget(contactsBox_, 1);
  auto *add = button(tr("Add contact"));
  auto *verify = button(tr("Check contact"));
  auto *share = button(tr("My contact card"));
  contactRow->addWidget(add);
  contactRow->addWidget(verify);
  contactRow->addWidget(share);
  wl->addLayout(contactRow);
  recipientInfo_ = label("");
  wl->addWidget(recipientInfo_);
  connect(add, &QPushButton::clicked, this, &Window::addContact);
  connect(verify, &QPushButton::clicked, this, &Window::verifyContact);
  connect(share, &QPushButton::clicked, this, &Window::shareCard);
  connect(contactsBox_, &QComboBox::currentIndexChanged, this, [this] {
    sealed_->clear();
    draft_->clear();
    copyButton_->setEnabled(false);
    refreshContacts();
  });
  auto *tabs = new QTabWidget;
  wl->addWidget(tabs, 1);
  auto *send = new QWidget;
  auto *sl = new QVBoxLayout(send);
  sl->addWidget(label(
      tr("1. Choose a checked contact above. 2. Write. 3. Encrypt and copy.")));
  draft_ = editor(tr("Write your message here…"));
  sl->addWidget(draft_, 1);
  sealButton_ = button(tr("Encrypt message"));
  sl->addWidget(sealButton_);
  sealed_ = editor(tr("Only the encrypted message appears here."), true);
  sealed_->setLayoutDirection(Qt::LeftToRight);
  sl->addWidget(sealed_, 1);
  copyButton_ = button(tr("Copy encrypted message"));
  copyButton_->setEnabled(false);
  sl->addWidget(copyButton_);
  tabs->addTab(send, tr("Write"));
  connect(sealButton_, &QPushButton::clicked, this, &Window::seal);
  connect(copyButton_, &QPushButton::clicked, this, &Window::copyCipher);
  connect(draft_, &QPlainTextEdit::textChanged, this, [this] {
    sealed_->clear();
    copyButton_->setEnabled(false);
  });
  auto *read = new QWidget;
  auto *rl = new QVBoxLayout(read);
  rl->addWidget(label(tr("Paste the entire encrypted message, including its "
                         "BEGIN and END lines.")));
  incoming_ = editor(tr("Paste encrypted message…"));
  incoming_->setLayoutDirection(Qt::LeftToRight);
  rl->addWidget(incoming_, 1);
  openButton_ = button(tr("Decrypt message"));
  rl->addWidget(openButton_);
  senderInfo_ =
      label(tr("The sender will be checked before the message is shown."));
  rl->addWidget(senderInfo_);
  plain_ = editor(tr("A verified message appears here."), true);
  rl->addWidget(plain_, 1);
  tabs->addTab(read, tr("Read"));
  connect(openButton_, &QPushButton::clicked, this, &Window::openMessage);
  connect(incoming_, &QPlainTextEdit::textChanged, this, [this] {
    plain_->clear();
    senderInfo_->setText(
        tr("The sender will be checked before the message is shown."));
  });
  pages_->addWidget(work_);
  status_ = label(tr("Ready"));
  status_->setAccessibleName(tr("Status"));
  layout->addWidget(status_);
  layout->addWidget(
      label(tr("Locks after 3 minutes, even while typing. Escape clears the "
               "view. This does not erase disk or memory traces.")));
  // Keep every foreground/background pair in the desktop palette, including
  // child dialogs and combo popups. Mixing fixed colors with native surfaces
  // makes controls unreadable under dark themes.
  setStyleSheet("QLabel#title{font-size:27px;font-weight:700;} "
                "QLabel#heading{font-size:21px;font-weight:600;} "
                "QPushButton{padding:6px 12px;} "
                "QPlainTextEdit,QComboBox{padding:8px;} "
                "QTabBar::tab{padding:9px 22px;}");
}
void Window::applyTheme(const QString &theme) {
  if (theme == "system") {
    qApp->setPalette(systemPalette_);
    return;
  }
  QPalette palette = qApp->style()->standardPalette();
  const bool dark = theme == "dark";
  const QColor surface = dark ? QColor("#202426") : QColor("#f4f6f8");
  const QColor field = dark ? QColor("#171b1d") : QColor("#ffffff");
  const QColor text = dark ? QColor("#f4f6f8") : QColor("#17283b");
  const QColor muted = dark ? QColor("#bdc8d0") : QColor("#526170");
  for (const auto group : {QPalette::Active, QPalette::Inactive,
                           QPalette::Disabled}) {
    palette.setColor(group, QPalette::Window, surface);
    palette.setColor(group, QPalette::Base, field);
    palette.setColor(group, QPalette::AlternateBase,
                     dark ? QColor("#242b2e") : QColor("#edf2f6"));
    palette.setColor(group, QPalette::Button, field);
    palette.setColor(group, QPalette::Text, text);
    palette.setColor(group, QPalette::WindowText, text);
    palette.setColor(group, QPalette::ButtonText, text);
    palette.setColor(group, QPalette::PlaceholderText, muted);
    palette.setColor(group, QPalette::ToolTipBase, field);
    palette.setColor(group, QPalette::ToolTipText, text);
    palette.setColor(group, QPalette::Highlight, QColor("#2a78b7"));
    palette.setColor(group, QPalette::HighlightedText, Qt::white);
    palette.setColor(group, QPalette::BrightText, Qt::white);
  }
  qApp->setPalette(palette);
}
void Window::job(std::function<TaskResult()> action,
                 std::function<void(TaskResult)> done) {
  if (busy_)
    return;
  busy_ = true;
  work_->setEnabled(false);
  gateButton_->setEnabled(false);
  languageBox_->setEnabled(false);
  status_->setText(
      tr("Working… A separate GnuPG window may ask for your passphrase."));
  const auto epoch = epoch_;
  auto *watcher = new QFutureWatcher<TaskResult>(this);
  connect(watcher, &QFutureWatcher<TaskResult>::finished, this,
          [this, watcher, done, epoch] {
            auto result = watcher->result();
            watcher->deleteLater();
            busy_ = false;
            work_->setEnabled(true);
            gateButton_->setEnabled(true);
            languageBox_->setEnabled(true);
            if (epoch != epoch_) {
              result.bytes.fill(0);
              result.opened.text.fill(0);
              crypto_.clearAgent();
              status_->setText(tr("Locked. Pending results were discarded."));
              return;
            }
            if (!result.error.isEmpty()) {
              status_->setText(tr("The operation could not be completed."));
              QMessageBox box(QMessageBox::Warning, tr("Operation failed"),
                              tr("Nothing was sent. Check the contact card, "
                                 "message and passphrase, then try again."),
                              QMessageBox::Ok, this);
              box.setDetailedText(result.error);
              box.exec();
              return;
            }
            status_->setText(tr("Ready"));
            done(result);
          });
  watcher->setFuture(QtConcurrent::run([action] {
    try {
      return action();
    } catch (const std::exception &e) {
      TaskResult r;
      r.error = QString::fromUtf8(e.what());
      return r;
    }
  }));
}
void Window::discover() {
  auto crypto = crypto_;
  job(
      [crypto] {
        Crypto::prepareHome(crypto.home());
        crypto.clearAgent();
        TaskResult r;
        r.keys = crypto.keys(true);
        return r;
      },
      [this](TaskResult r) {
        if (r.keys.size() > 1) {
          gateText_->setText(
              tr("More than one local identity was found. Setup needs manual "
                 "review; QChat will not choose one silently."));
          gateButton_->setEnabled(false);
          return;
        }
        own_ = r.keys.isEmpty() ? QString() : r.keys.first().fingerprint;
        gateText_->setText(
            own_.isEmpty()
                ? tr("Create your private identity on this computer. Choose a "
                     "strong passphrase in the separate GnuPG window. You do "
                     "not need an email address.")
                : tr("Unlock with your identity passphrase in the separate "
                     "GnuPG window. Contact names are loaded only after "
                     "unlocking."));
        gateButton_->setText(own_.isEmpty() ? tr("Set up QChat")
                                            : tr("Unlock"));
      });
}
void Window::unlock() {
  if (smoke_)
    return;
  const auto crypto = crypto_;
  const auto own = own_;
  job(
      [crypto, own] {
        TaskResult r;
        if (own.isEmpty())
          r.id = crypto.createIdentity();
        else {
          crypto.clearAgent();
          crypto.authenticate(own);
          r.id = own;
        }
        return r;
      },
      [this](TaskResult r) {
        own_ = r.id;
        locked_ = false;
        loadContacts();
        refreshContacts();
        pages_->setCurrentIndex(1);
        sessionTimer_.start();
        gateButton_->setText(tr("Unlock"));
        gateText_->setText(
            tr("Unlock with your identity passphrase in the separate GnuPG "
               "window. Contact names are loaded only after unlocking."));
      });
}
void Window::lock() {
  ++epoch_;
  locked_ = true;
  sessionTimer_.stop();
  draft_->clear();
  sealed_->clear();
  incoming_->clear();
  plain_->clear();
  senderInfo_->clear();
  recipientInfo_->clear();
  contacts_.clear();
  {
    QSignalBlocker b(contactsBox_);
    contactsBox_->clear();
  }
  copyButton_->setEnabled(false);
  clearClipboard();
  for (auto *dialog : findChildren<QDialog *>())
    dialog->reject();
  pages_->setCurrentIndex(0);
  if (!smoke_ && !crypto_.clearAgent())
    status_->setText(tr("View cleared, but GnuPG could not be stopped. Cached "
                        "access may remain."));
  else
    status_->setText(tr("Locked. No secure deletion was performed."));
}
void Window::loadContacts() {
  contacts_.clear();
  QFile file(path_ + "/contacts.json");
  if (!file.exists())
    return;
  if (!file.open(QIODevice::ReadOnly)) {
    status_->setText(tr("Contacts could not be loaded."));
    return;
  }
  QJsonParseError e;
  auto doc = QJsonDocument::fromJson(file.readAll(), &e);
  if (e.error != QJsonParseError::NoError || !doc.isArray()) {
    status_->setText(tr("Contacts could not be loaded."));
    return;
  }
  QSet<QString> seen;
  for (const auto &v : doc.array()) {
    auto o = v.toObject();
    auto fp = o["fingerprint"].toString();
    if (!QRegularExpression("^[A-F0-9]{40,64}$").match(fp).hasMatch() ||
        seen.contains(fp))
      continue;
    seen.insert(fp);
    contacts_.push_back(
        {o["name"].toString().left(80), fp, o["verified"].toBool(false)});
  }
}
bool Window::saveContacts() {
  QJsonArray a;
  for (const auto &c : contacts_)
    a.append(QJsonObject{{"name", c.name},
                         {"fingerprint", c.fingerprint},
                         {"verified", c.verified}});
  QSaveFile file(path_ + "/contacts.json");
  if (!file.open(QIODevice::WriteOnly))
    return false;
  file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
  auto data = QJsonDocument(a).toJson();
  return file.write(data) == data.size() && file.commit();
}
void Window::refreshContacts() {
  int i = contactsBox_->currentIndex();
  if (contactsBox_->count() != contacts_.size()) {
    QSignalBlocker block(contactsBox_);
    contactsBox_->clear();
    for (const auto &c : contacts_)
      contactsBox_->addItem(c.name);
    if (!contacts_.isEmpty())
      contactsBox_->setCurrentIndex(qBound(0, i, int(contacts_.size()) - 1));
    i = contactsBox_->currentIndex();
  }
  bool valid = i >= 0 && i < contacts_.size();
  sealButton_->setEnabled(valid && contacts_[i].verified && !locked_);
  recipientInfo_->setText(
      !valid ? tr("Add a contact card to get started.")
             : (contacts_[i].verified
                    ? tr("Checked contact: %1").arg(contacts_[i].name)
                    : tr("Contact not checked. Compare the full check code "
                         "before sending.")));
}
void Window::addContact() {
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Add contact"));
  auto *l = new QVBoxLayout(&dialog);
  l->addWidget(label(tr("Ask the other person for their public contact card. "
                        "It starts with BEGIN PGP PUBLIC KEY BLOCK. Never ask "
                        "for their private key or passphrase.")));
  auto *name = new QLineEdit;
  name->setPlaceholderText(tr("A nickname for this contact"));
  name->setMaxLength(80);
  l->addWidget(name);
  auto *card = editor(tr("Paste contact card here…"));
  card->setLayoutDirection(Qt::LeftToRight);
  l->addWidget(card);
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  l->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  dialog.resize(650, 430);
  if (dialog.exec() != QDialog::Accepted || locked_)
    return;
  const auto alias = name->text().trimmed();
  const auto bytes = normalizeCiphertext(card->toPlainText());
  if (alias.isEmpty() || bytes.size() > maxInput)
    return;
  auto crypto = crypto_;
  job(
      [crypto, bytes] {
        TaskResult r;
        r.id = crypto.importCard(bytes);
        return r;
      },
      [this, alias](TaskResult r) {
        if (r.id == own_) {
          status_->setText(tr(
              "This is your own contact card. Add the other person's card "
              "instead."));
          return;
        }
        for (const auto &contact : contacts_)
          if (contact.fingerprint == r.id) {
            status_->setText(tr("This contact card is already present."));
            return;
          }
        contacts_.push_back({alias, r.id, false});
        if (!saveContacts()) {
          contacts_.removeLast();
          status_->setText(tr("Contact could not be saved."));
          return;
        }
        status_->setText(tr("Contact added. Check their code before sending."));
        refreshContacts();
        contactsBox_->setCurrentIndex(int(contacts_.size()) - 1);
  });
}
void Window::verifyContact() {
  const int i = contactsBox_->currentIndex();
  if (i < 0 || i >= contacts_.size())
    return;
  const auto c = contacts_[i];
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Check contact"));
  auto *l = new QVBoxLayout(&dialog);
  l->addWidget(label(
      tr("Compare this entire code through a second trusted route, such as a "
         "known website or a conversation you can authenticate. Comparing it "
         "only in the same untrusted chat does not prevent replacement.")));
  auto *fp = label(code(c.fingerprint));
  fp->setLayoutDirection(Qt::LeftToRight);
  fp->setTextInteractionFlags(Qt::TextSelectableByMouse);
  l->addWidget(fp);
  auto *confirm = new QCheckBox(
      tr("I compared the whole code through a trusted route; it matches."));
  l->addWidget(confirm);
  auto *b =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  b->button(QDialogButtonBox::Ok)->setEnabled(false);
  l->addWidget(b);
  connect(confirm, &QCheckBox::toggled, b->button(QDialogButtonBox::Ok),
          &QPushButton::setEnabled);
  connect(b, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(b, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  dialog.resize(630, 250);
  if (dialog.exec() != QDialog::Accepted || locked_)
    return;
  contacts_[i].verified = true;
  if (!saveContacts()) {
    contacts_[i].verified = c.verified;
    status_->setText(tr("Contact could not be saved."));
  }
  refreshContacts();
}
void Window::shareCard() {
  auto crypto = crypto_;
  auto own = own_;
  job(
      [crypto, own] {
        TaskResult r;
        r.bytes = crypto.exportCard(own);
        return r;
      },
      [this](TaskResult r) {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("My contact card"));
        auto *l = new QVBoxLayout(&dialog);
        l->addWidget(label(
            tr("Share this card so others can write to you. It contains no "
               "private key. It can still link your contacts to you.")));
        auto *fp = label(code(own_));
        fp->setLayoutDirection(Qt::LeftToRight);
        fp->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->addWidget(fp);
        auto *text = editor("", true);
        text->setLayoutDirection(Qt::LeftToRight);
        text->setPlainText(QString::fromUtf8(r.bytes));
        l->addWidget(text);
        auto *copy = button(tr("Copy contact card"));
        l->addWidget(copy);
        connect(copy, &QPushButton::clicked, this, [this, r] {
          QApplication::clipboard()->setText(QString::fromUtf8(r.bytes));
          clipboardHash_ =
              QCryptographicHash::hash(r.bytes, QCryptographicHash::Sha256);
          clipboardTimer_.start();
        });
        auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
        l->addWidget(close);
        connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.resize(650, 480);
        dialog.exec();
      });
}
void Window::seal() {
  const int i = contactsBox_->currentIndex();
  if (i < 0 || i >= contacts_.size() || !contacts_[i].verified)
    return;
  auto bytes = draft_->toPlainText().toUtf8();
  if (bytes.isEmpty() || bytes.size() > maxInput) {
    status_->setText(tr("Enter a message smaller than 1 MiB."));
    return;
  }
  sealed_->clear();
  copyButton_->setEnabled(false);
  auto crypto = crypto_;
  auto own = own_;
  auto recipient = contacts_[i].fingerprint;
  job(
      [crypto, bytes, own, recipient] {
        TaskResult r;
        r.bytes = crypto.seal(bytes, recipient, own);
        return r;
      },
      [this](TaskResult r) {
        draft_->clear();
        sealed_->setPlainText(QString::fromUtf8(r.bytes));
        copyButton_->setEnabled(true);
        status_->setText(tr("Encrypted and signed. Copy the block into your "
                            "usual communication channel."));
      });
}
void Window::openMessage() {
  auto bytes = normalizeCiphertext(incoming_->toPlainText());
  if (bytes.isEmpty() || bytes.size() > 2 * maxInput) {
    status_->setText(tr("Paste an encrypted message smaller than 2 MiB."));
    return;
  }
  plain_->clear();
  auto crypto = crypto_;
  job(
      [crypto, bytes] {
        TaskResult r;
        r.opened = crypto.open(bytes);
        return r;
      },
      [this](TaskResult r) {
        if (!r.opened.valid && r.opened.text.isEmpty()) {
          senderInfo_->setText(
              tr("Not shown: decryption did not produce a message. Check the "
                 "key and the complete encrypted block."));
          return;
        }
        QString name;
        if (r.opened.signer == own_)
          name = tr("You");
        else
          for (const auto &c : contacts_)
            if (c.fingerprint == r.opened.signer && c.verified)
              name = c.name;
        if (!r.opened.valid)
          senderInfo_->setText(tr(
              "Decrypted, but the sender is not authenticated. Do not treat "
              "this as verified."));
        else if (name.isEmpty())
          senderInfo_->setText(tr(
              "Decrypted, but this sender has not been checked. Check their "
              "contact card before trusting the identity."));
        else
          senderInfo_->setText(tr("Valid signature — %1").arg(name));
        plain_->setPlainText(QString::fromUtf8(r.opened.text));
        r.opened.text.fill(0);
      });
}
QByteArray Window::normalizeCiphertext(const QString &input) {
  QString text = input;
  text.remove(QChar::ByteOrderMark);
  text.replace("\r\n", "\n");
  text = text.trimmed();
  if (text.startsWith("```") && text.endsWith("```")) {
    const auto firstLine = text.indexOf('\n');
    if (firstLine >= 0)
      text = text.mid(firstLine + 1, text.size() - firstLine - 4).trimmed();
  }
  // Chat renderers may copy a code block with a fixed indentation on every
  // line. OpenPGP armor does not accept whitespace before its delimiters or
  // encoded lines, so remove only leading spaces/tabs, never Base64 content.
  auto lines = text.split('\n');
  for (auto &line : lines)
    line.remove(QRegularExpression("^[ \\t]+"));
  text = lines.join('\n').trimmed();
  return text.toUtf8();
}
void Window::copyCipher() {
  auto text = sealed_->toPlainText();
  if (text.isEmpty())
    return;
  QApplication::clipboard()->setText(text);
  clipboardHash_ =
      QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256);
  clipboardTimer_.start();
  status_->setText(
      tr("Encrypted message copied. QChat will try to clear this clipboard "
         "entry after 45 seconds; clipboard histories may retain it."));
}
void Window::clearClipboard() {
  clipboardTimer_.stop();
  auto *cb = QApplication::clipboard();
  if (!clipboardHash_.isEmpty() &&
      QCryptographicHash::hash(cb->text().toUtf8(),
                               QCryptographicHash::Sha256) == clipboardHash_)
    cb->clear();
  clipboardHash_.clear();
  if (cb->supportsSelection() && cb->ownsSelection())
    cb->clear(QClipboard::Selection);
}
void Window::help() {
  QMessageBox box(this);
  box.setWindowTitle(tr("Safety & help"));
  box.setTextFormat(Qt::PlainText);
  box.setText(tr(
      "QChat is an experimental local encryption helper, not a secure "
      "messenger or an anonymity tool.\n\nIt does not send messages or keep a "
      "message history. Public contact cards, nicknames and check status are "
      "stored locally and can reveal associations. Private keys are protected "
      "by your GnuPG passphrase.\n\nUse full-disk encryption. An app lock "
      "cannot protect an unlocked or compromised computer. Clipboard managers, "
      "swap, backups and crash dumps may retain information. There is no "
      "secure-delete button. USB monitoring, when available, triggers an app "
      "lock but does not block devices.\n\nA stolen "
      "usable private key can decrypt previously recorded messages. Signatures "
      "do not prove a person's real-world identity or prevent "
      "replay.\n\nLosing the local identity or its passphrase loses access to "
      "old messages. This version does not provide backup or key renewal. "
      "Start with non-sensitive test messages.\n\nPersian translation is a "
      "draft requiring native-speaker review."));
  box.exec();
}
void Window::closeEvent(QCloseEvent *e) {
  lock();
  if (busy_) {
    status_->setText(tr("Waiting for the crypto operation to stop. Close again "
                        "when it finishes."));
    e->ignore();
  } else
    e->accept();
}
