#include "port_alloc.h"

namespace droppix {
int allocate_port(int base, const std::set<int>& used, int cap) {
  for (int k = 0; k < cap; ++k) {
    const int p = base + k;
    if (used.find(p) == used.end()) return p;
  }
  return -1;
}
}  // namespace droppix
