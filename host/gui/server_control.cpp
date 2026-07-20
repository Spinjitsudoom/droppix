#include "server_control.h"

namespace droppix {

bool shouldRearm(bool enabled, qint64 elapsedMs, bool everConnected) {
  if (!enabled) return false;
  return everConnected || elapsedMs >= kServerMinRunMs;
}

}  // namespace droppix
