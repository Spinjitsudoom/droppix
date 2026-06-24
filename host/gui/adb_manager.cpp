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
}  // namespace droppix
