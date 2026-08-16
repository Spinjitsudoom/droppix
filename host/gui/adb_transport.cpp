#include "adb_transport.h"

#include <QHash>
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

AdbTransport::AdbTransport(QObject* parent) : QObject(parent) {
  qRegisterMetaType<QList<droppix::AdbClient>>("QList<droppix::AdbClient>");
  timer_.setInterval(kPollMs);
  connect(&timer_, &QTimer::timeout, this, &AdbTransport::poll);
}

void AdbTransport::start(int port) {
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

void AdbTransport::stop() {
  timer_.stop();
  tunnelled_.clear();
  if (!clients_.isEmpty()) { clients_.clear(); emit clientsChanged(clients_); }
  // Leave the tunnels themselves in place: `adb reverse --remove` would break a session
  // that is streaming over one right now, and a stale tunnel is harmless.
}

void AdbTransport::poll() {
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

    for (const QString& serial : present) {
      if (!tunnelled_.contains(serial)) applyTo(serial);
      if (!models_.contains(serial)) resolveModel(serial);
    }

    // Rebuild the advertised list and emit only on change, so the GUI's device list does
    // not churn (and lose the user's selection) twice a second.
    QList<AdbClient> next;
    for (const QString& serial : present)
      next.push_back({serial, models_.value(serial, serial)});
    std::sort(next.begin(), next.end(),
              [](const AdbClient& a, const AdbClient& b) { return a.serial < b.serial; });
    bool changed = next.size() != clients_.size();
    for (int i = 0; !changed && i < next.size(); ++i)
      changed = next[i].serial != clients_[i].serial || next[i].model != clients_[i].model;
    if (changed) { clients_ = next; emit clientsChanged(clients_); }
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

void AdbTransport::applyTo(const QString& serial) {
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

void AdbTransport::resolveModel(const QString& serial) {
  // Cache a placeholder immediately so a slow/failed lookup cannot queue one process per
  // poll tick for the same device.
  models_.insert(serial, serial);
  auto* p = new QProcess(this);
  connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [this, p, serial](int, QProcess::ExitStatus) {
    const QString m = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
    if (!m.isEmpty()) models_.insert(serial, m);
    p->deleteLater();
  });
  connect(p, &QProcess::errorOccurred, this, [p](QProcess::ProcessError) { p->deleteLater(); });
  p->start("adb", {"-s", serial, "shell", "getprop", "ro.product.model"});
}

void AdbTransport::usbConnect(const QString& serial, int port) {
  const QString t = QString("tcp:%1").arg(port);
  auto* rev = new QProcess(this);
  connect(rev, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [this, rev, serial, port](int, QProcess::ExitStatus) {
    rev->deleteLater();
    tunnelled_.insert(serial);
    // The tunnel must exist before the client dials, so the launch is chained off the
    // reverse finishing rather than fired alongside it.
    auto* app = new QProcess(this);
    connect(app, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, app, serial](int code, QProcess::ExitStatus st) {
      if (st != QProcess::NormalExit || code != 0)
        emit message(QString("USB: could not launch the client on %1 — is droppix installed?")
                         .arg(serial));
      app->deleteLater();
    });
    connect(app, &QProcess::errorOccurred, this, [app](QProcess::ProcessError) { app->deleteLater(); });
    app->start("adb", {"-s", serial, "shell", "am", "start",
                       "-n", "com.droppix.app/.ui.ConnectActivity",
                       "--ez", "usb_autoconnect", "true",
                       "--ei", "usb_port", QString::number(port)});
  });
  connect(rev, &QProcess::errorOccurred, this, [rev](QProcess::ProcessError) { rev->deleteLater(); });
  rev->start("adb", {"-s", serial, "reverse", t, t});
}

}  // namespace droppix
