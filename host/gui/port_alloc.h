#pragma once
#include <set>

namespace droppix {
// Lowest base+k (0 <= k < cap) not already in `used`; -1 if all cap slots are taken.
// Used by the GUI to give each streaming session its own port (base = the configured port).
int allocate_port(int base, const std::set<int>& used, int cap = 4);
}  // namespace droppix
