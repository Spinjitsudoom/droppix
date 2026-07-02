#pragma once
#include <cstdint>
#include <vector>
#include "protocol.h"   // TouchContact
#include "mt_slots.h"

namespace droppix {
// Multi-touch uinput TOUCHSCREEN (evdev protocol B, INPUT_PROP_DIRECT). KWin binds it to the
// droppix output (via the outputName DBus property), so the device's 0..65535 ABS range maps
// directly onto that monitor. inject() is given the FULL set of active contacts each event.
class InputInjector {
 public:
  ~InputInjector();
  bool open();  // needs root /dev/uinput
  bool ok() const { return fd_ >= 0; }
  void inject(const std::vector<TouchContact>& contacts);
 private:
  int fd_ = -1;
  MtSlots slots_;
  bool anyDown_ = false;   // last BTN_TOUCH state, so we only emit it on a transition
};
}  // namespace droppix
