#include "session_params.h"
namespace droppix {
SessionParams select_session_params(uint32_t cver, uint32_t hfps, uint8_t haudio, uint8_t hori,
                                    uint32_t hbitrate, int dfps, bool daudio, int dori, int dbitrate) {
  if (cver >= 4) {
    return { hfps > 0 ? static_cast<int>(hfps) : dfps,
             haudio != 0,
             static_cast<int>(hori & 3),
             (cver >= 5 && hbitrate > 0) ? static_cast<int>(hbitrate) : dbitrate };
  }
  return { dfps, daudio, dori, dbitrate };
}
}  // namespace droppix
