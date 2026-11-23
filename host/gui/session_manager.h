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
  QString id;          // tablet device id (cross-transport identity; may be empty)
  bool mirror = false; // true = mirroring an existing display; false = extended virtual monitor
};

// Bookkeeping for the set of live sessions. No Qt event handling — pure container logic,
// so it is unit-tested with controller == nullptr.
class SessionManager {
 public:
  bool has(const QString& key) const;
  Session* find(const QString& key);            // nullptr if none
  std::set<int> usedPorts() const;
  QSet<QString> keys() const;   // keys of all active sessions
  QSet<QString> ids() const;    // non-empty device ids of active sessions
  int allocatePort(int base, bool reserveBase = false) const {
    return allocate_port(base, usedPorts(), 4, reserveBase);
  }
  int count() const { return static_cast<int>(sessions_.size()); }
  Session& add(const Session& s);               // appends; ref valid until the next mutation
  void remove(const QString& key);
  QList<Session>& list() { return sessions_; }

 private:
  QList<Session> sessions_;
};
}  // namespace droppix
