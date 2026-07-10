#include "host_store.h"
#include <QSettings>

namespace droppix {
namespace {
QSettings& settings() {
  static QSettings s("droppix", "droppix_client_hosts");
  return s;
}
}  // namespace

QList<KnownHost> HostStore::hosts() const {
  QList<KnownHost> out;
  int n = settings().beginReadArray("hosts");
  for (int i = 0; i < n; ++i) {
    settings().setArrayIndex(i);
    KnownHost h;
    h.host = settings().value("host").toString();
    h.port = static_cast<quint16>(settings().value("port").toUInt());
    h.label = settings().value("label").toString();
    if (h.label.isEmpty()) h.label = QString("%1:%2").arg(h.host).arg(h.port);
    out.push_back(h);
  }
  settings().endArray();
  return out;
}

void HostStore::remember(const QString& host, quint16 port, const QString& label) {
  auto existing = hosts();
  bool found = false;
  for (auto& h : existing) {
    if (h.host == host && h.port == port) {
      if (!label.isEmpty()) h.label = label;
      found = true;
      break;
    }
  }
  if (!found) {
    existing.push_back({label.isEmpty() ? QString("%1:%2").arg(host).arg(port) : label,
                        host, port});
  }
  settings().beginWriteArray("hosts");
  for (int i = 0; i < existing.size(); ++i) {
    settings().setArrayIndex(i);
    settings().setValue("host", existing[i].host);
    settings().setValue("port", existing[i].port);
    settings().setValue("label", existing[i].label);
  }
  settings().endArray();
}

void HostStore::forget(const QString& host, quint16 port) {
  auto existing = hosts();
  QList<KnownHost> kept;
  for (const auto& h : existing) if (!(h.host == host && h.port == port)) kept.push_back(h);
  settings().beginWriteArray("hosts");
  for (int i = 0; i < kept.size(); ++i) {
    settings().setArrayIndex(i);
    settings().setValue("host", kept[i].host);
    settings().setValue("port", kept[i].port);
    settings().setValue("label", kept[i].label);
  }
  settings().endArray();
}

}  // namespace droppix
