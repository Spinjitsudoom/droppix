#pragma once
#include <QList>
#include <QSet>
#include <QString>

namespace droppix {

// A discovered device the host could auto-connect. `eligible` is precomputed by
// the caller: USB = app-bearing (always true); net = TXT id in the approved store.
struct AutoConnectCandidate {
  QString key;            // "usb-aoa:<serial>" or "net:<address>"
  QString id;             // tablet device id (cross-transport identity; may be empty)
  bool eligible = false;
};

// An active session, for cross-transport arbitration (key prefix tells the transport).
struct ActiveSessionRef {
  QString key;
  QString id;             // may be empty
};

// What auto-connect should do this pass. USB (cable) is always preferred over net for
// the same device id: `connect` never contains a net key for an id that also has an
// eligible USB candidate, and `disconnect` lists net-session keys whose tablet just
// became reachable over an eligible USB candidate (stop them; the USB candidate is
// connected on the follow-up evaluation once the net session is gone).
struct AutoConnectPlan {
  QList<QString> connect;
  QList<QString> disconnect;
};

AutoConnectPlan devicesToConnect(bool enabled,
                                 const QList<AutoConnectCandidate>& candidates,
                                 const QList<ActiveSessionRef>& active);

}  // namespace droppix
