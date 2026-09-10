#pragma once
#include "crypto.h"
#include <QMainWindow>
#include <QPalette>
#include <QTimer>
#include <functional>
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QComboBox;
class QStackedWidget;
class QTabWidget;
struct Contact {
  QString name;
  QString fingerprint;
  bool verified = false;
};
struct TaskResult {
  QString error;
  QString id;
  QByteArray bytes;
  Opened opened;
  QVector<Identity> keys;
};
class Window : public QMainWindow {
  Q_OBJECT
  friend class WindowTest;
  friend class ThemeTest;

public:
  explicit Window(QString dataPath, bool smoke = false);
  ~Window() override;

protected:
  void closeEvent(QCloseEvent *event) override;

private:
  Crypto crypto_;
  QString path_, own_;
  QPalette systemPalette_;
  QVector<Contact> contacts_;
  bool busy_ = false, locked_ = true, smoke_ = false;
  unsigned epoch_ = 0;
  QTimer sessionTimer_, clipboardTimer_;
  QByteArray clipboardHash_;
  QStackedWidget *pages_;
  QWidget *work_;
  QLabel *gateText_, *status_, *recipientInfo_, *senderInfo_;
  QPushButton *gateButton_, *sealButton_, *openButton_, *copyButton_,
      *lockButton_;
  QComboBox *contactsBox_, *languageBox_, *themeBox_;
  QPlainTextEdit *draft_, *sealed_, *incoming_, *plain_;
  void build();
  void discover();
  void job(std::function<TaskResult()> action,
           std::function<void(TaskResult)> done);
  void unlock();
  void lock();
  void loadContacts();
  bool saveContacts();
  void refreshContacts();
  void addContact();
  void verifyContact();
  void shareCard();
  void seal();
  void openMessage();
  void copyCipher();
  void clearClipboard();
  void help();
  void applyTheme(const QString &theme);
  static QString code(const QString &fingerprint);
  static QByteArray normalizeCiphertext(const QString &input);
};
