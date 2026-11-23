#include "session_params.h"
namespace droppix {
int clamp_fps(uint32_t fps) {
  if (fps < 1) return 1;
  return fps > static_cast<uint32_t>(kMaxSessionFps) ? kMaxSessionFps : static_cast<int>(fps);
}

int clamp_bitrate_kbps(uint32_t kbps) {
  if (kbps < static_cast<uint32_t>(kMinSessionBitrateKbps)) return kMinSessionBitrateKbps;
  if (kbps > static_cast<uint32_t>(kMaxSessionBitrateKbps)) return kMaxSessionBitrateKbps;
  return static_cast<int>(kbps);
}

SessionParams select_session_params(uint32_t cver, uint32_t hfps, uint8_t haudio, uint8_t hori,
                                    uint32_t hbitrate, int dfps, bool daudio, int dori, int dbitrate) {
  if (cver >= 4) {
    return { hfps > 0 ? clamp_fps(hfps) : dfps,
             haudio != 0,
             static_cast<int>(hori & 3),
             (cver >= 5 && hbitrate > 0) ? clamp_bitrate_kbps(hbitrate) : dbitrate };
  }
  return { dfps, daudio, dori, dbitrate };
}
}  // namespace droppix
