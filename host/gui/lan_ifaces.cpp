#include "lan_ifaces.h"
#include <QNetworkInterface>

namespace droppix {

QList<LanIface> lan_ipv4_ifaces() {
  QList<LanIface> out;
  for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
    if (!(iface.flags() & QNetworkInterface::IsUp) ||
        !(iface.flags() & QNetworkInterface::IsRunning) ||
        (iface.flags() & QNetworkInterface::IsLoopBack))
      continue;
    for (const QNetworkAddressEntry& e : iface.addressEntries()) {
      const QHostAddress a = e.ip();
      if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback())
        out.push_back({a.toString(), iface.humanReadableName()});
    }
  }
  return out;
}

QList<LanIface> included_ifaces(const QList<LanIface>& all, const QSet<QString>& excludedNames) {
  QList<LanIface> out;
  for (const LanIface& i : all)
    if (!excludedNames.contains(i.name)) out.push_back(i);
  return out;
}

}  // namespace droppix
