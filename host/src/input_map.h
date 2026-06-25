#pragma once
#include <cstdint>
namespace droppix {
struct Rect { int x = 0, y = 0, w = 0, h = 0; };
struct AbsCoord { int x = 0, y = 0; };
// Map a normalized touch (0..65535 within `monitor`) to a uinput ABS value
// (0..65535) spanning the whole desktop.
AbsCoord map_to_abs(uint16_t x_norm, uint16_t y_norm,
                    const Rect& monitor, int desktop_w, int desktop_h);
}  // namespace droppix
