#pragma once
#include <QList>
#include <QSet>
#include <QString>

namespace droppix {

struct LanIface {
  QString ip;    // IPv4 dotted string
  QString name;  // human-readable adapter name
};

// All up + running, non-loopback IPv4 interfaces (system query; not unit-tested).
QList<LanIface> lan_ipv4_ifaces();

// Pure filter: keep interfaces whose name is NOT in excludedNames, order preserved.
QList<LanIface> included_ifaces(const QList<LanIface>& all, const QSet<QString>& excludedNames);

}  // namespace droppix
