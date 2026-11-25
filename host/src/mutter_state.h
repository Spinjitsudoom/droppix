#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "monitor_geometry.h"   // OutputInfo

namespace droppix {

// GNOME's display layout as reported by org.gnome.Mutter.DisplayConfig.GetCurrentState.
struct MutterMode {
  std::string id;          // e.g. "2560x1600@59.972" — what ApplyMonitorsConfig takes
  int w = 0, h = 0;
  bool current = false;
};
struct MutterMonitor {
  std::string connector, vendor, product, serial;   // EDID identity as Mutter reports it
  std::vector<MutterMode> modes;
  const MutterMode* current_mode() const;
};
struct MutterLogical {
  int x = 0, y = 0;
  double scale = 1.0;
  uint32_t transform = 0;  // 0 normal, 1 90°, 2 180°, 3 270°, 4–7 flipped variants
  bool primary = false;
  std::vector<std::string> connectors;   // more than one = mirrored
};
struct MutterState {
  uint32_t serial = 0;           // must be echoed back to ApplyMonitorsConfig
  std::vector<MutterMonitor> monitors;
  std::vector<MutterLogical> logical;
  uint32_t layout_mode = 1;      // 1 logical (Wayland default), 2 physical
};

// PURE (unit-tested). Parses `gdbus call ... GetCurrentState` output.
bool parse_mutter_state(const std::string& text, MutterState& out);

// PURE (unit-tested). One OutputInfo per monitor, in layout coordinates (logical pixels in
// logical layout mode, so a 2560x1600 output at 200 % is 1280x800). Monitors with no
// logical monitor are listed disabled.
std::vector<OutputInfo> mutter_outputs(const MutterState& st);

// PURE (unit-tested). Connector whose EDID serial is the session's (droppix writes the
// session port as the EDID serial); accepts decimal or 0x-hex text. "" if none.
std::string mutter_find_by_serial(const MutterState& st, uint32_t session_serial);

// PURE (unit-tested). The logical-monitors argument for ApplyMonitorsConfig, as GVariant
// text: the current layout with `connector` moved to (x, y). "" if the connector is not
// active. Each monitor keeps its current mode, scale, transform and primary flag.
std::string mutter_place_config(const MutterState& st, const std::string& connector, int x, int y);

// PURE (unit-tested). Same, but `connector` joins the primary's logical monitor (GNOME's
// mirror), both at the largest resolution they share. "" if there is no shared mode.
std::string mutter_mirror_config(const MutterState& st, const std::string& connector);

// PURE (unit-tested). The current layout without `connector` (dropped from a mirrored
// logical monitor, or its own logical monitor removed). "" if it is not active, or if
// nothing would be left on.
std::string mutter_disable_config(const MutterState& st, const std::string& connector);

// PURE (unit-tested). The `gsettings set` command that binds one uinput device (vendor
// 0x1209, product `product_id`) to monitor `m`: the touchscreen schema, or the tablet
// schema when `pen`. The value is m's EDID identity exactly as Mutter reports it. "" if
// that text holds anything that could break out of the quoted value.
std::string gnome_map_command(bool pen, uint16_t product_id, const MutterMonitor& m);

}  // namespace droppix
