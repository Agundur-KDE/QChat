#pragma once
#include <QObject>
struct udev;
struct udev_monitor;
class QSocketNotifier;
class SystemWatch : public QObject {
  Q_OBJECT
public:
  explicit SystemWatch(QObject *parent = nullptr);
  ~SystemWatch() override;
  bool usbAvailable() const { return monitor_ != nullptr; }
signals:
  void protectionRequested();
private slots:
  void activeChanged(bool active);

private:
  udev *udev_ = nullptr;
  udev_monitor *monitor_ = nullptr;
  QSocketNotifier *notifier_ = nullptr;
};
