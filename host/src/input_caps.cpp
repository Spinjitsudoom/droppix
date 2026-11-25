#include "input_caps.h"
#include <linux/input-event-codes.h>

namespace droppix {

DeviceCaps pen_device_caps() {
  // 0..65535 over roughly a 10" tablet's long side (~250 mm) -> ~262 units/mm. Only
  // plausibility matters: the compositor maps the device onto the droppix output.
  constexpr int kUnitsPerMm = 262;
  DeviceCaps d;
  d.props = {INPUT_PROP_DIRECT};
  d.keys = {BTN_TOOL_PEN, BTN_TOOL_RUBBER, BTN_TOUCH, BTN_STYLUS};
  d.abs = {{ABS_X, 0, 65535, kUnitsPerMm},
           {ABS_Y, 0, 65535, kUnitsPerMm},
           {ABS_PRESSURE, 0, 1023, 0}};
  return d;
}

}  // namespace droppix
