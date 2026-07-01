#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QList>
#include <QString>
#include <QStringList>
#include <QMetaType>
#include <functional>

namespace droppix {

// A droppix client reachable over USB (adb). `model` is a human label
// (ro.product.model); `serial` is the adb device serial used for `adb -s`.
struct UsbClient {
  QString serial;
  QString model;
};

// Periodically discovers USB-connected tablets that have the droppix app
// installed: `adb devices` -> for each ready device, `adb -s <serial> shell pm
// list packages com.droppix.app` -> `getprop ro.product.model`. Fully async
// (chained short-lived QProcess on the UI thread); never blocks. Mirrors
// MdnsBrowser. Emits the client list plus a status hint for the unhappy cases
// (a device present but `unauthorized`).
class UsbClientScanner : public QObject {
  Q_OBJECT
 public:
  explicit UsbClientScanner(QObject* p = nullptr);
  bool available() const;        // adb on PATH
  void start();
  void stop();
 signals:
  void clientsChanged(QList<droppix::UsbClient> clients);
  void statusChanged(QString status);   // "" when fine; else an adb hint
 private:
  void runScan();
  void checkNextDevice();
  void finishScan(const QString& status);
  // Runs `adb <args>` and calls cb with stdout (a null QString on start failure).
  void runCmd(const QStringList& args, std::function<void(QString)> cb);

  QTimer timer_;
  bool busy_ = false;
  QList<UsbClient> acc_;        // clients found so far this scan
  QStringList pending_;         // serials still to check this scan
  bool anyUnauthorized_ = false;
};

}  // namespace droppix

Q_DECLARE_METATYPE(droppix::UsbClient)
