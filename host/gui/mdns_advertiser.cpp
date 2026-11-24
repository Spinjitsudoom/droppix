#include "mdns_advertiser.h"
#include <QStandardPaths>
#include <QHostInfo>
#include "process_lifetime.h"
namespace droppix {
// A killed GUI used to leave its announcement running for good, so tablets kept
// seeing stale duplicate "(droppix)" hosts.
MdnsAdvertiser::MdnsAdvertiser(QObject* p) : QObject(p) { endWithParent(proc_); }
bool MdnsAdvertiser::available() const {
  return !QStandardPaths::findExecutable("avahi-publish-service").isEmpty();
}
void MdnsAdvertiser::start(quint16 port) {
  if (!available() || proc_.state() != QProcess::NotRunning) return;
  const QString name = QHostInfo::localHostName() + " (droppix)";
  proc_.start("avahi-publish-service",
              {name, "_droppix._tcp", QString::number(port)});
}
void MdnsAdvertiser::stop() {
  if (proc_.state() != QProcess::NotRunning) { proc_.terminate();
    if (!proc_.waitForFinished(1500)) proc_.kill(); }
}
}  // namespace droppix
