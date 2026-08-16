#include "adb_reverse.h"

#include <QProcess>
#include <QStringList>

namespace droppix {
namespace {
constexpr int kPollMs = 2000;   // same cadence as the tether/AOA scanners
}

QSet<QString> parse_ready_serials(const QString& devices_output) {
  QSet<QString> ready;
  for (const QString& line : devices_output.split('\n')) {
    const QString l = line.trimmed();
    if (l.isEmpty() || l.startsWith("List of devices")) continue;
    if (l.startsWith('*')) continue;            // "* daemon started successfully *"
    const QStringList cols = l.split('\t');
    if (cols.size() < 2) continue;
    if (cols[1].trimmed() != "device") continue;   // skip unauthorized / offline
    ready.insert(cols[0].trimmed());
  }
  return ready;
}

AdbReverse::AdbReverse(QObject* parent) : QObject(parent) {
  timer_.setInterval(kPollMs);
  connect(&timer_, &QTimer::timeout, this, &AdbReverse::poll);
}

void AdbReverse::start(int port) {
  if (port <= 0) return;
  if (port != port_) {
    // A different port means every existing tunnel points at the wrong place.
    port_ = port;
    tunnelled_.clear();
  }
  if (adbMissing_) return;
  poll();                       // don't make the first device wait a whole tick
  if (!timer_.isActive()) timer_.start();
}

void AdbReverse::stop() {
  timer_.stop();
  tunnelled_.clear();
  // Leave the tunnels themselves in place: `adb reverse --remove` would break a session
  // that is streaming over one right now, and a stale tunnel is harmless.
}

void AdbReverse::poll() {
  auto* p = new QProcess(this);
  connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [this, p](int, QProcess::ExitStatus) {
    const QString out = QString::fromUtf8(p->readAllStandardOutput());
    p->deleteLater();

    const QSet<QString> present = parse_ready_serials(out);

    // Forget devices that went away, so a replug re-applies the tunnel rather than being
    // skipped as "already done".
    for (auto it = tunnelled_.begin(); it != tunnelled_.end();)
      it = present.contains(*it) ? std::next(it) : tunnelled_.erase(it);

    for (const QString& serial : present)
      if (!tunnelled_.contains(serial)) applyTo(serial);
  });
  connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError) {
    p->deleteLater();
    if (adbMissing_) return;
    adbMissing_ = true;
    timer_.stop();
    emit message("adb not found — the client's USB button needs it; tether/AOA still work");
  });
  p->start("adb", {"devices"});
}

void AdbReverse::applyTo(const QString& serial) {
  const QString t = QString("tcp:%1").arg(port_);
  auto* p = new QProcess(this);
  connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [this, p, serial](int code, QProcess::ExitStatus st) {
    const bool ok = (st == QProcess::NormalExit && code == 0);
    if (ok) {
      tunnelled_.insert(serial);
      emit message(QString("USB: %1 can now reach this PC on 127.0.0.1:%2").arg(serial).arg(port_));
    } else {
      // Left out of tunnelled_, so the next poll retries.
      emit message(QString("USB: adb reverse failed for %1 — %2")
                       .arg(serial, QString::fromUtf8(p->readAllStandardError()).trimmed()));
    }
    p->deleteLater();
  });
  connect(p, &QProcess::errorOccurred, this, [p](QProcess::ProcessError) { p->deleteLater(); });
  p->start("adb", {"-s", serial, "reverse", t, t});
}

}  // namespace droppix
