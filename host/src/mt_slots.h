#pragma once
#include <cstdint>
#include <map>
#include <vector>
#include "protocol.h"   // TouchContact

namespace droppix {

// Maps app pointer ids to kernel multi-touch slots (evdev protocol B). Pure, no I/O.
// Each update() is given the FULL set of currently-active contacts and returns which slots
// to release (ids that vanished) and which to (re)assign — isNew marks a fresh contact that
// needs a new ABS_MT_TRACKING_ID. Contacts beyond the slot budget are dropped.
class MtSlots {
 public:
  explicit MtSlots(int maxSlots = 10) : maxSlots_(maxSlots) {}
  struct Assign { int slot; TouchContact c; bool isNew; };
  struct Update { std::vector<Assign> active; std::vector<int> lifted; };
  Update update(const std::vector<TouchContact>& contacts);

 private:
  int maxSlots_;
  std::map<uint8_t, int> idSlot_;   // active pointer id -> slot
};

}  // namespace droppix
