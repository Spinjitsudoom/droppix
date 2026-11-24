#include "mdns_browser.h"
#include <QStandardPaths>
#include "process_lifetime.h"

namespace droppix {

MdnsBrowser::MdnsBrowser(QObject* p) : QObject(p) {
  qRegisterMetaType<droppix::MdnsDevice>("droppix::MdnsDevice");
  qRegisterMetaType<QList<droppix::MdnsDevice>>("QList<droppix::MdnsDevice>");
  endWithParent(proc_);
  timer_.setInterval(3000);
  connect(&timer_, &QTimer::timeout, this, &MdnsBrowser::runBrowse);
}

bool MdnsBrowser::available() const {
  return !QStandardPaths::findExecutable("avahi-browse").isEmpty();
}

void MdnsBrowser::start() {
  if (!available()) return;
  timer_.start();
  runBrowse();
}

void MdnsBrowser::stop() {
  timer_.stop();
  if (proc_.state() != QProcess::NotRunning) {
    proc_.kill();
    proc_.waitForFinished(1500);
  }
}

void MdnsBrowser::runBrowse() {
  if (proc_.state() != QProcess::NotRunning) return;  // previous run still in flight
  QObject::disconnect(&proc_, nullptr, this, nullptr);
  connect(&proc_, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
    const QString output = QString::fromUtf8(proc_.readAllStandardOutput());
    const auto devices = parse_avahi_browse(output.toStdString());
    emit devicesChanged(QList<droppix::MdnsDevice>(devices.begin(), devices.end()));
  });
  proc_.start("avahi-browse", {"-rptk", "_droppix-client._tcp"});
}

}  // namespace droppix
