#include "usb_client_scanner.h"
#include "usb_scan.h"
#include <QStandardPaths>
#include <memory>

namespace droppix {

static const char* kPackage = "com.droppix.app";

UsbClientScanner::UsbClientScanner(QObject* p) : QObject(p) {
  qRegisterMetaType<droppix::UsbClient>("droppix::UsbClient");
  qRegisterMetaType<QList<droppix::UsbClient>>("QList<droppix::UsbClient>");
  timer_.setInterval(3000);
  connect(&timer_, &QTimer::timeout, this, &UsbClientScanner::runScan);
}

bool UsbClientScanner::available() const {
  return !QStandardPaths::findExecutable("adb").isEmpty();
}

void UsbClientScanner::start() {
  if (!available()) { emit statusChanged("adb not found"); return; }
  timer_.start();
  runScan();
}

void UsbClientScanner::stop() {
  timer_.stop();
}

void UsbClientScanner::runCmd(const QStringList& args, std::function<void(QString)> cb) {
  auto* p = new QProcess(this);
  auto done = std::make_shared<bool>(false);
  auto finish = [p, cb, done](QString out) {
    if (*done) return;   // finished + errorOccurred can both fire; call cb once
    *done = true;
    p->deleteLater();
    cb(out);
  };
  connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [p, finish](int, QProcess::ExitStatus) {
            finish(QString::fromUtf8(p->readAllStandardOutput()));
          });
  connect(p, &QProcess::errorOccurred, this,
          [finish](QProcess::ProcessError) { finish(QString()); });  // null => failed
  p->start("adb", args);
}

void UsbClientScanner::runScan() {
  if (busy_) return;                 // previous scan still chaining
  busy_ = true;
  acc_.clear();
  pending_.clear();
  anyUnauthorized_ = false;
  runCmd({"devices"}, [this](QString out) {
    if (out.isNull()) { finishScan("adb not found"); return; }
    for (const auto& d : parse_adb_devices(out.toStdString())) {
      if (d.state == "unauthorized") anyUnauthorized_ = true;
      else if (d.state == "device") pending_ << QString::fromStdString(d.serial);
    }
    checkNextDevice();
  });
}

void UsbClientScanner::checkNextDevice() {
  if (pending_.isEmpty()) {
    QString status;
    if (acc_.isEmpty() && anyUnauthorized_)
      status = "USB device unauthorized — accept the prompt on the tablet";
    finishScan(status);
    return;
  }
  const QString serial = pending_.takeFirst();
  runCmd({"-s", serial, "shell", "pm", "list", "packages", kPackage},
         [this, serial](QString pm) {
    if (!pm.isNull() && adb_package_present(pm.toStdString(), kPackage)) {
      runCmd({"-s", serial, "shell", "getprop", "ro.product.model"},
             [this, serial](QString model) {
        const QString m = model.trimmed();
        acc_.append(UsbClient{serial, m.isEmpty() ? serial : m});
        checkNextDevice();
      });
    } else {
      checkNextDevice();
    }
  });
}

void UsbClientScanner::finishScan(const QString& status) {
  busy_ = false;
  emit statusChanged(status);
  emit clientsChanged(acc_);
}

}  // namespace droppix
