#pragma once
#include <set>

namespace droppix {
// Lowest base+k (0 <= k < cap) not already in `used`; -1 if all cap slots are taken.
// Used by the GUI to give each streaming session its own port (base = the configured port).
// `reserve_base` skips base itself: it belongs to the Server listener, which is the port
// mDNS advertises and the adb tunnel points at.
int allocate_port(int base, const std::set<int>& used, int cap = 4, bool reserve_base = false);
}  // namespace droppix
