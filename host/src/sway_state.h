#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "monitor_geometry.h"   // OutputInfo

namespace droppix {

struct SwayOutput {
  OutputInfo info;      // name, enabled (= active), layout rect (logical px, Sway applies scale)
  std::string serial;   // EDID serial as Sway reports it ("0x00006978", "Unknown", ...)
};

// PURE (unit-tested). `swaymsg -t get_outputs -r`.
bool parse_sway_outputs(const std::string& json, std::vector<SwayOutput>& out);

// PURE (unit-tested). Name of the output whose EDID serial is the session's, or "".
std::string sway_find_by_serial(const std::vector<SwayOutput>& outs, uint32_t session_serial);

// PURE (unit-tested). The swaymsg command list that puts `name` at (x, y) and pins every other
// active output at its current position (Sway auto-arranges unpinned outputs after pinned
// ones, which pushed the main screen right of the tablet). "" if `name` is unknown/unsafe.
std::string sway_place_command(const std::vector<SwayOutput>& outs, const std::string& name,
                               int x, int y);

// PURE (unit-tested). `swaymsg -t get_inputs -r`: the identifier ("vendor:product:name") of the
// input device called `name`, for `swaymsg input <identifier> ...`. "" if absent, or if it
// holds characters that could escape a shell argument.
std::string sway_input_identifier(const std::string& json, const std::string& name);

// PURE (unit-tested). The live Sway IPC socket among a runtime dir's entries
// ("sway-ipc.<uid>.<pid>.sock"): the one for `uid` whose pid `alive` says is running.
std::string pick_sway_socket(const std::vector<std::string>& entries, long uid,
                             const std::function<bool(long pid)>& alive);

}  // namespace droppix
