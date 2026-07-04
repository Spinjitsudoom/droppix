#pragma once
#include <QList>
#include <QSet>
#include <QString>

namespace droppix {

// A discovered device the host could auto-connect. `eligible` is precomputed by
// the caller: USB = app-bearing (always true); net = TXT id in the approved store.
struct AutoConnectCandidate {
  QString key;            // "usb:<serial>" or "net:<address>"
  bool eligible = false;
};

// Keys to auto-connect now: enabled AND eligible AND not already active.
QList<QString> devicesToConnect(bool enabled,
                                const QList<AutoConnectCandidate>& candidates,
                                const QSet<QString>& activeKeys);

}  // namespace droppix
