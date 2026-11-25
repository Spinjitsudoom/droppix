#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "monitor_geometry.h"   // OutputInfo

namespace droppix {

struct HyprMonitor {
  OutputInfo info;          // layout rect: mode size / scale, w/h swapped for 90°/270°
  std::string serial;
  int mode_w = 0, mode_h = 0;
  double refresh = 0, scale = 1;
  int transform = 0;
};

// PURE (unit-tested). `hyprctl -j monitors all`.
bool parse_hypr_monitors(const std::string& json, std::vector<HyprMonitor>& out);

// PURE (unit-tested). Monitor whose EDID serial is the session's, or "".
std::string hypr_find_by_serial(const std::vector<HyprMonitor>& mons, uint32_t session_serial);

// PURE (unit-tested). A `hyprctl keyword monitor <rule>` rule putting `name` at (x, y) with its
// current mode, scale and transform. "" if unknown/unsafe.
std::string hypr_place_rule(const std::vector<HyprMonitor>& mons, const std::string& name, int x, int y);

// PURE (unit-tested). The same rule as Lua for `hyprctl eval`: Hyprland 0.5x configs are Lua
// (hyprland.lua), where `keyword` answers "unknown request". "" if unknown/unsafe.
std::string hypr_place_lua(const std::vector<HyprMonitor>& mons, const std::string& name, int x, int y);
std::string hypr_device_lua(const std::string& device, const std::string& output);

// PURE (unit-tested). The live Hyprland instance signature among `signatures` (directory names
// under $XDG_RUNTIME_DIR/hypr): the one whose lock-file pid (`pid_of`) `alive` says is running.
std::string pick_hypr_instance(const std::vector<std::string>& signatures,
                               const std::function<long(const std::string&)>& pid_of,
                               const std::function<bool(long)>& alive);

}  // namespace droppix
