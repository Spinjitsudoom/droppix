#pragma once
#include <QList>
#include <QSet>
#include <QString>
#include <set>
#include "port_alloc.h"

namespace droppix {
class StreamController;

// One live streaming session = one tablet = one extended monitor.
struct Session {
  StreamController* controller = nullptr;
  int port = 0;
  QString touchName;   // droppix-touch-<port>
  QString key;         // client identity (id / ip / adb serial) — one session per key
  QString label;       // display name
  QString transport;   // "usb" | "net"
};

// Bookkeeping for the set of live sessions. No Qt event handling — pure container logic,
// so it is unit-tested with controller == nullptr.
class SessionManager {
 public:
  bool has(const QString& key) const;
  Session* find(const QString& key);            // nullptr if none
  std::set<int> usedPorts() const;
  QSet<QString> keys() const;   // keys of all active sessions
  int allocatePort(int base) const { return allocate_port(base, usedPorts()); }
  int count() const { return static_cast<int>(sessions_.size()); }
  Session& add(const Session& s);               // appends; ref valid until the next mutation
  void remove(const QString& key);
  QList<Session>& list() { return sessions_; }

 private:
  QList<Session> sessions_;
};
}  // namespace droppix
