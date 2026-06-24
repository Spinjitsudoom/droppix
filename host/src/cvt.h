#pragma once
#include "edid.h"   // droppix::Timing

namespace droppix {
// VESA CVT reduced-blanking v1 timing for width x height @ refresh_hz.
// h_active is rounded down to the 8-px cell granularity.
Timing cvt_rb_timing(int width, int height, int refresh_hz);

// The EDID timing to advertise for a mode: the verified CEA preset for the
// 1920x1080@60 default, CVT reduced-blanking otherwise.
Timing mode_timing(int width, int height, int refresh_hz);
}  // namespace droppix
