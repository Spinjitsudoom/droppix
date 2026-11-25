#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "monitor_geometry.h"   // OutputInfo

namespace droppix {

struct CosmicOutput {
  OutputInfo info;       // layout rect: current mode / scale, swapped for 90°/270°
  std::string serial;    // EDID serial ("0x00006978")
  std::string mirroring; // source output when this one mirrors it (then geom = the source's)
};

// PURE (unit-tested). `cosmic-randr list --kdl`. The output flagged xwayland_primary is primary.
bool parse_cosmic_outputs(const std::string& kdl, std::vector<CosmicOutput>& out);

// PURE (unit-tested). Output whose EDID serial is the session's, or "".
std::string cosmic_find_by_serial(const std::vector<CosmicOutput>& outs, uint32_t session_serial);

// PURE (unit-tested). The output touch/pen for `name` must map to: its mirror source when it
// is mirroring (a mirror isn't in COSMIC's layout, so mapping to it drops input), else itself.
std::string cosmic_touch_target(const std::vector<CosmicOutput>& outs, const std::string& name);

// PURE (unit-tested). cosmic-comp's per-device input config (RON map keyed by device name, at
// ~/.config/cosmic/com.system76.CosmicComp/v1/input_devices) with `device` mapped to `output`.
// Every other entry, including the user's own devices, is kept verbatim. "" if a name is
// unsafe or `existing` can't be parsed (then the user's file is left alone).
std::string cosmic_input_devices(const std::string& existing, const std::string& device,
                                 const std::string& output);

}  // namespace droppix
