#include "mt_slots.h"
#include <set>

namespace droppix {

MtSlots::Update MtSlots::update(const std::vector<TouchContact>& contacts) {
  Update u;

  std::set<uint8_t> now;
  for (const auto& c : contacts) now.insert(c.id);

  // Release slots whose contact id disappeared.
  for (auto it = idSlot_.begin(); it != idSlot_.end();) {
    if (now.count(it->first) == 0) { u.lifted.push_back(it->second); it = idSlot_.erase(it); }
    else ++it;
  }

  std::set<int> used;
  for (const auto& kv : idSlot_) used.insert(kv.second);
  auto freeSlot = [&]() -> int {
    for (int s = 0; s < maxSlots_; ++s) if (used.count(s) == 0) return s;
    return -1;
  };

  for (const auto& c : contacts) {
    auto it = idSlot_.find(c.id);
    if (it != idSlot_.end()) {
      u.active.push_back({it->second, c, false});
    } else {
      int s = freeSlot();
      if (s < 0) continue;              // over the slot budget: drop this contact
      idSlot_[c.id] = s; used.insert(s);
      u.active.push_back({s, c, true});
    }
  }
  return u;
}

}  // namespace droppix
