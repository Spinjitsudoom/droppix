#pragma once
#include <string>
#include <vector>
#include "input_map.h"   // droppix::Rect
namespace droppix {
struct OutputInfo { std::string name; Rect geom; bool enabled = false; };
std::vector<OutputInfo> parse_kscreen_outputs(const std::string& text);
Rect desktop_bounds(const std::vector<OutputInfo>& outs);   // {0,0,maxRight,maxBottom}
// Select the droppix output: an enabled output whose size == mode, preferring a
// name containing evdi/Unknown/droppix. Returns false if none match.
bool select_droppix(const std::vector<OutputInfo>& outs, int mode_w, int mode_h, Rect& out);
}  // namespace droppix
