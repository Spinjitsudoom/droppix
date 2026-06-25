#include "input_map.h"
#include <cmath>
namespace droppix {
static int map_axis(uint16_t norm, int off, int size, int desktop) {
  if (desktop <= 0) return 0;
  const double frac = norm / 65535.0;
  const double global = off + frac * size;
  int v = static_cast<int>(std::lround(global / desktop * 65535.0));
  if (v < 0) v = 0;
  if (v > 65535) v = 65535;
  return v;
}
AbsCoord map_to_abs(uint16_t x_norm, uint16_t y_norm,
                    const Rect& monitor, int desktop_w, int desktop_h) {
  return AbsCoord{ map_axis(x_norm, monitor.x, monitor.w, desktop_w),
                   map_axis(y_norm, monitor.y, monitor.h, desktop_h) };
}
}  // namespace droppix
