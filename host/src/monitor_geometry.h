#pragma once
#include <string>
#include <vector>
#include "input_map.h"   // droppix::Rect
namespace droppix {
struct OutputInfo { std::string name; Rect geom; bool enabled = false; };
std::vector<OutputInfo> parse_kscreen_outputs(const std::string& text);
// Parse `xrandr --query` text (X11 backend). Connected outputs with an active mode
// ("<name> connected [primary] WxH+X+Y ...") are enabled with geometry; connected-but-
// inactive and disconnected outputs are included disabled.
std::vector<OutputInfo> parse_xrandr_outputs(const std::string& text);
Rect desktop_bounds(const std::vector<OutputInfo>& outs);   // {0,0,maxRight,maxBottom}
// Select the droppix output: an enabled output whose size == mode, preferring an
// evdi-signature name (evdi/Unknown/droppix, or X11's secondary-GPU "DVI-I-1-<n>").
// The `before` overload excludes outputs that were already enabled before the source
// started from the plain size match (a same-resolution physical screen must never win).
bool select_droppix(const std::vector<OutputInfo>& outs, int mode_w, int mode_h, Rect& out);
bool select_droppix(const std::vector<OutputInfo>& outs, int mode_w, int mode_h, OutputInfo& out);
bool select_droppix(const std::vector<OutputInfo>& outs, const std::vector<OutputInfo>& before,
                    int mode_w, int mode_h, OutputInfo& out);
// Robust identification: the enabled output present in `after` but not in `before`
// (by name) is the just-created droppix monitor, regardless of its size. Returns
// false if there is no single new output (caller falls back to select_droppix).
bool select_new_output(const std::vector<OutputInfo>& before,
                       const std::vector<OutputInfo>& after, Rect& out);
bool select_new_output(const std::vector<OutputInfo>& before,
                       const std::vector<OutputInfo>& after, OutputInfo& out);
}  // namespace droppix
