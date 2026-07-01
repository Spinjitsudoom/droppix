#include "adb_manager.h"
#include <QProcess>

namespace droppix {

AdbManager::AdbManager(QObject* parent) : QObject(parent) {}

void AdbManager::refresh() {
  auto* p = new QProcess(this);
  connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this, [this, p](int, QProcess::ExitStatus) {
    QString out = QString::fromUtf8(p->readAllStandardOutput());
    QString status = "no device";
    for (const QString& line : out.split('\n')) {
      const QString l = line.trimmed();
      if (l.isEmpty() || l.startsWith("List of devices")) continue;
      if (l.endsWith("device")) { status = l.section('\t', 0, 0) + " — connected"; break; }
      if (l.endsWith("unauthorized")) { status = l.section('\t', 0, 0) + " — unauthorized"; break; }
    }
    emit deviceStatus(status);
    p->deleteLater();
  });
  connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError) {
    emit deviceStatus("adb not found");
    p->deleteLater();
  });
  p->start("adb", {"devices"});
}

void AdbManager::setupReverse(int port) {
  auto* p = new QProcess(this);
  const QString t = QString("tcp:%1").arg(port);
  connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this, [p](int, QProcess::ExitStatus){ p->deleteLater(); });
  connect(p, &QProcess::errorOccurred, this, [p](QProcess::ProcessError){ p->deleteLater(); });
  p->start("adb", {"reverse", t, t});
}

void AdbManager::usbConnect(const QString& serial, int port) {
  const QString t = QString("tcp:%1").arg(port);
  // 1) reverse tunnel so the tablet can dial the PC over 127.0.0.1:<port>
  auto* rev = new QProcess(this);
  connect(rev, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [this, rev, serial, port](int, QProcess::ExitStatus) {
    rev->deleteLater();
    // 2) launch the app pointed at localhost (usb_autoconnect => connectTo 127.0.0.1)
    auto* app = new QProcess(this);
    connect(app, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [app](int, QProcess::ExitStatus){ app->deleteLater(); });
    connect(app, &QProcess::errorOccurred, this, [app](QProcess::ProcessError){ app->deleteLater(); });
    app->start("adb", {"-s", serial, "shell", "am", "start",
                       "-n", "com.droppix.app/.ui.ConnectActivity",
                       "--ez", "usb_autoconnect", "true",
                       "--ei", "usb_port", QString::number(port)});
  });
  connect(rev, &QProcess::errorOccurred, this, [rev](QProcess::ProcessError){ rev->deleteLater(); });
  rev->start("adb", {"-s", serial, "reverse", t, t});
}
}  // namespace droppix
