#pragma once
#include <vector>

namespace droppix {

struct AbsAxis { int code, min, max, resolution; };   // resolution: units per mm (0 = unset)
struct DeviceCaps {
  std::vector<int> props;   // INPUT_PROP_*
  std::vector<int> keys;    // EV_KEY codes
  std::vector<AbsAxis> abs; // EV_ABS axes
};

// PURE (unit-tested). The stylus uinput device's capabilities. libinput (GNOME and KDE)
// ignores a tablet that lacks BTN_STYLUS or an X/Y resolution, so both are declared even
// though droppix maps the device onto its output and never relies on physical size.
DeviceCaps pen_device_caps();

}  // namespace droppix
