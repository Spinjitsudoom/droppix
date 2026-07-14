#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "protocol.h"   // TouchContact
#include "mt_slots.h"
#include "tap_gesture.h"

namespace droppix {
// Multi-touch uinput TOUCHSCREEN (evdev protocol B, INPUT_PROP_DIRECT). KWin binds it to the
// droppix output, so the device's 0..65535 ABS range maps directly onto that monitor.
// inject() is given the FULL set of active contacts each event. A two-finger TAP is detected
// and synthesized as a right-click on a second (absolute pointer) device.
class InputInjector {
 public:
  ~InputInjector();
  bool open(const std::string& name = "droppix-touch");  // needs root /dev/uinput (the MT touchscreen)
  bool ok() const { return fd_ >= 0; }
  void inject(const std::vector<TouchContact>& contacts);
  // Records the droppix output rect within the desktop and creates the right-click pointer
  // (a desktop-ranged absolute device) used for two-finger-tap -> BTN_RIGHT. No-op when the
  // desktop bounds are unknown — right-click stays disabled; normal touch is unaffected.
  void set_geometry(int out_x, int out_y, int out_w, int out_h, int desktop_w, int desktop_h);
  // Scroll wheel and extra mouse buttons on the same absolute pointer device used for
  // right-click. x_norm/y_norm are 0..65535 on the droppix output, same as right_click.
  void scroll(int dx, int dy, uint16_t x_norm, uint16_t y_norm);
  void mouse_button(uint8_t button, bool down, uint16_t x_norm, uint16_t y_norm);
  // Emits a single key event (EV_KEY) on the keyboard uinput device. action follows the
  // Linux input value convention: 0=up, 1=down, 2=repeat. No-op if the keyboard device
  // failed to create (kb_fd_ < 0) — keyboard input is optional, touch stays primary.
  void key(uint16_t keycode, uint8_t action);
  // Emits a stylus sample on the pen uinput device (a graphics tablet: BTN_TOOL_PEN/RUBBER +
  // ABS_PRESSURE, DIRECT + ABS 0..65535 bound to the droppix output). touching/eraser drive
  // BTN_TOOL_PEN vs BTN_TOOL_RUBBER proximity edges. No-op if the pen device failed to create
  // (pen_fd_ < 0) — pen input is optional, touch stays primary.
  void pen(uint16_t x, uint16_t y, uint16_t pressure, bool touching, bool eraser);

 private:
  void right_click(uint16_t x_norm, uint16_t y_norm);
  // Shared 0..65535 -> desktop-pixel scaling used by right_click/scroll/mouse_button.
  int scale_x(uint16_t x_norm) const;
  int scale_y(uint16_t y_norm) const;
  int fd_ = -1;        // multi-touch touchscreen
  int rc_fd_ = -1;     // right-click absolute pointer
  int kb_fd_ = -1;     // keyboard
  int pen_fd_ = -1;    // stylus/pen tablet
  bool pen_down_ = false;    // last BTN_TOOL_PEN/RUBBER proximity state
  bool pen_eraser_ = false;  // which tool is in proximity (for the up edge)
  MtSlots slots_;
  bool anyDown_ = false;   // last BTN_TOUCH state
  TwoFingerTap tap_;
  int outX_ = 0, outY_ = 0, outW_ = 0, outH_ = 0, deskW_ = 0, deskH_ = 0;
};
}  // namespace droppix
