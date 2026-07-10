#pragma once
#include <QString>
#include <QList>

namespace droppix {

struct KnownHost {
  QString label;   // user-facing name, defaults to "host:port"
  QString host;
  quint16 port = 27000;
};

// Remembers hosts the user has connected to (QSettings-backed), so a returning host
// shows up in the connect dialog without re-typing address/port. Pairing state itself
// (the TLS fingerprint pin) lives separately in TlsTrust — this is just the UI-facing
// address book, mirroring host/gui/approved_store.cpp's role on the other side.
class HostStore {
 public:
  QList<KnownHost> hosts() const;
  void remember(const QString& host, quint16 port, const QString& label = QString());
  void forget(const QString& host, quint16 port);
};

}  // namespace droppix
