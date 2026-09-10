#include "systemwatch.h"
#include <QDBusConnection>
#include <QSocketNotifier>
#include <cstring>
#include <libudev.h>
SystemWatch::SystemWatch(QObject *parent) : QObject(parent) {
  udev_ = udev_new();
  if (udev_) {
    monitor_ = udev_monitor_new_from_netlink(udev_, "udev");
    if (monitor_ && (udev_monitor_filter_add_match_subsystem_devtype(
                         monitor_, "usb", "usb_device") < 0 ||
                     udev_monitor_enable_receiving(monitor_) < 0)) {
      udev_monitor_unref(monitor_);
      monitor_ = nullptr;
    }
    if (monitor_) {
      notifier_ = new QSocketNotifier(udev_monitor_get_fd(monitor_),
                                      QSocketNotifier::Read, this);
      connect(notifier_, &QSocketNotifier::activated, this, [this] {
        while (auto *device = udev_monitor_receive_device(monitor_)) {
          auto action = udev_device_get_action(device);
          bool added = action && std::strcmp(action, "add") == 0;
          udev_device_unref(device);
          if (added)
            emit protectionRequested();
        }
      });
    }
  }
  auto session = QDBusConnection::sessionBus();
  session.connect("org.freedesktop.ScreenSaver", "/ScreenSaver",
                  "org.freedesktop.ScreenSaver", "ActiveChanged", this,
                  SLOT(activeChanged(bool)));
  session.connect("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
                  "org.freedesktop.ScreenSaver", "ActiveChanged", this,
                  SLOT(activeChanged(bool)));
  session.connect("org.gnome.ScreenSaver", "/org/gnome/ScreenSaver",
                  "org.gnome.ScreenSaver", "ActiveChanged", this,
                  SLOT(activeChanged(bool)));
  session.connect("org.mate.ScreenSaver", "/org/mate/ScreenSaver",
                  "org.mate.ScreenSaver", "ActiveChanged", this,
                  SLOT(activeChanged(bool)));
  QDBusConnection::systemBus().connect(
      "org.freedesktop.login1", "/org/freedesktop/login1",
      "org.freedesktop.login1.Manager", "PrepareForSleep", this,
      SLOT(activeChanged(bool)));
}
SystemWatch::~SystemWatch() {
  delete notifier_;
  if (monitor_)
    udev_monitor_unref(monitor_);
  if (udev_)
    udev_unref(udev_);
}
void SystemWatch::activeChanged(bool active) {
  if (active)
    emit protectionRequested();
}
