#pragma once
#include <cstdint>
#include "input_map.h"
namespace droppix {
// Absolute uinput pointer; maps normalized touch onto the droppix monitor.
class InputInjector {
 public:
  ~InputInjector();
  bool open(const Rect& monitor, int desktop_w, int desktop_h);  // needs root /dev/uinput
  bool ok() const { return fd_ >= 0; }
  void inject(uint8_t action, uint16_t x_norm, uint16_t y_norm);
 private:
  int fd_ = -1;
  Rect monitor_;
  int desktop_w_ = 0, desktop_h_ = 0;
};
}  // namespace droppix
