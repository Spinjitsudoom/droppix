#include "auto_connect.h"

namespace droppix {

QList<QString> devicesToConnect(bool enabled,
                                const QList<AutoConnectCandidate>& candidates,
                                const QSet<QString>& activeKeys) {
  QList<QString> out;
  if (!enabled) return out;
  for (const auto& c : candidates)
    if (c.eligible && !activeKeys.contains(c.key)) out.push_back(c.key);
  return out;
}

}  // namespace droppix
