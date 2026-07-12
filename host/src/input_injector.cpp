#include "input_injector.h"
#include <linux/uinput.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstring>
#include <cstdio>
#include <chrono>
#include <ctime>
#include <algorithm>

namespace droppix {
namespace {
int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}
void emit(int fd, int type, int code, int val) {
  input_event ev{};
  ev.type = type; ev.code = code; ev.value = val;
  ssize_t n = ::write(fd, &ev, sizeof(ev));
  (void)n;
}
}  // namespace

bool InputInjector::open(const std::string& name) {
  fd_ = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (fd_ < 0) { std::fprintf(stderr, "uinput open failed (need root); input disabled\n"); return false; }

  // Declare a multi-touch TOUCHSCREEN (evdev protocol B, absolute/direct) rather than a bare
  // ABS pointer, which libinput would treat as a touchpad. ABS_MT_* carry the per-finger
  // contacts; ABS_X/Y/PRESSURE mirror the primary finger for single-touch emulation.
  ioctl(fd_, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
  ioctl(fd_, UI_SET_EVBIT, EV_KEY);
  ioctl(fd_, UI_SET_KEYBIT, BTN_TOUCH);
  ioctl(fd_, UI_SET_EVBIT, EV_ABS);
  for (int code : {ABS_X, ABS_Y, ABS_PRESSURE,
                   ABS_MT_SLOT, ABS_MT_TRACKING_ID, ABS_MT_POSITION_X, ABS_MT_POSITION_Y,
                   ABS_MT_PRESSURE}) {
    ioctl(fd_, UI_SET_ABSBIT, code);
  }

  auto abs = [&](int code, int min, int max) {
    uinput_abs_setup a{}; a.code = code; a.absinfo.minimum = min; a.absinfo.maximum = max;
    ioctl(fd_, UI_ABS_SETUP, &a);
  };
  abs(ABS_X, 0, 65535); abs(ABS_Y, 0, 65535); abs(ABS_PRESSURE, 0, 1023);
  abs(ABS_MT_SLOT, 0, 9);                 // up to 10 simultaneous contacts
  abs(ABS_MT_TRACKING_ID, 0, 65535);
  abs(ABS_MT_POSITION_X, 0, 65535); abs(ABS_MT_POSITION_Y, 0, 65535);
  abs(ABS_MT_PRESSURE, 0, 1023);

  uinput_setup us{};
  us.id.bustype = BUS_USB; us.id.vendor = 0x1209; us.id.product = 0xd701;
  std::strncpy(us.name, name.c_str(), sizeof(us.name) - 1);
  if (ioctl(fd_, UI_DEV_SETUP, &us) < 0 || ioctl(fd_, UI_DEV_CREATE) < 0) {
    std::fprintf(stderr, "uinput device create failed; input disabled\n");
    ::close(fd_); fd_ = -1; return false;
  }
  return true;
}

void InputInjector::inject(const std::vector<TouchContact>& contacts) {
  if (fd_ < 0) return;
  // Device is bound to the droppix output, so 0..65535 spans that monitor directly.
  const MtSlots::Update u = slots_.update(contacts);

  for (int slot : u.lifted) {                       // release vanished fingers
    emit(fd_, EV_ABS, ABS_MT_SLOT, slot);
    emit(fd_, EV_ABS, ABS_MT_TRACKING_ID, -1);
  }
  for (const auto& a : u.active) {
    emit(fd_, EV_ABS, ABS_MT_SLOT, a.slot);
    if (a.isNew) emit(fd_, EV_ABS, ABS_MT_TRACKING_ID, a.c.id);
    emit(fd_, EV_ABS, ABS_MT_POSITION_X, a.c.x);
    emit(fd_, EV_ABS, ABS_MT_POSITION_Y, a.c.y);
    emit(fd_, EV_ABS, ABS_MT_PRESSURE, a.c.pressure);
  }

  const bool anyDown = !contacts.empty();
  if (anyDown != anyDown_) { emit(fd_, EV_KEY, BTN_TOUCH, anyDown ? 1 : 0); anyDown_ = anyDown; }
  if (anyDown) {                                     // single-touch emulation from the primary finger
    const TouchContact& p = contacts.front();
    emit(fd_, EV_ABS, ABS_X, p.x);
    emit(fd_, EV_ABS, ABS_Y, p.y);
    emit(fd_, EV_ABS, ABS_PRESSURE, p.pressure);
  }
  emit(fd_, EV_SYN, SYN_REPORT, 0);

  // Two-finger tap -> right-click on the pointer device (if geometry is known).
  const TwoFingerTap::Result t = tap_.update(contacts, now_ms());
  if (t.rightClick) right_click(t.x, t.y);
}

void InputInjector::set_geometry(int out_x, int out_y, int out_w, int out_h,
                                 int desktop_w, int desktop_h) {
  outX_ = out_x; outY_ = out_y; outW_ = out_w; outH_ = out_h;
  deskW_ = desktop_w; deskH_ = desktop_h;
  if (rc_fd_ >= 0 || deskW_ <= 0 || deskH_ <= 0) return;   // created once; needs desktop bounds

  rc_fd_ = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (rc_fd_ < 0) { std::fprintf(stderr, "right-click: uinput open failed; disabled\n"); return; }
  // Absolute pointer spanning the whole desktop; we place it in desktop pixels + click RIGHT.
  ioctl(rc_fd_, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
  ioctl(rc_fd_, UI_SET_EVBIT, EV_KEY);
  ioctl(rc_fd_, UI_SET_KEYBIT, BTN_LEFT);
  ioctl(rc_fd_, UI_SET_KEYBIT, BTN_RIGHT);
  ioctl(rc_fd_, UI_SET_KEYBIT, BTN_MIDDLE);
  ioctl(rc_fd_, UI_SET_EVBIT, EV_REL);
  ioctl(rc_fd_, UI_SET_RELBIT, REL_WHEEL);
  ioctl(rc_fd_, UI_SET_RELBIT, REL_HWHEEL);
  ioctl(rc_fd_, UI_SET_EVBIT, EV_ABS);
  ioctl(rc_fd_, UI_SET_ABSBIT, ABS_X);
  ioctl(rc_fd_, UI_SET_ABSBIT, ABS_Y);
  uinput_abs_setup rx{}; rx.code = ABS_X; rx.absinfo.minimum = 0; rx.absinfo.maximum = deskW_ - 1;
  ioctl(rc_fd_, UI_ABS_SETUP, &rx);
  uinput_abs_setup ry{}; ry.code = ABS_Y; ry.absinfo.minimum = 0; ry.absinfo.maximum = deskH_ - 1;
  ioctl(rc_fd_, UI_ABS_SETUP, &ry);
  uinput_setup us{};
  us.id.bustype = BUS_USB; us.id.vendor = 0x1209; us.id.product = 0xd702;
  std::strncpy(us.name, "droppix-rightclick", sizeof(us.name) - 1);
  if (ioctl(rc_fd_, UI_DEV_SETUP, &us) < 0 || ioctl(rc_fd_, UI_DEV_CREATE) < 0) {
    std::fprintf(stderr, "right-click: uinput device create failed; disabled\n");
    ::close(rc_fd_); rc_fd_ = -1;
  }
}

// Map a tap/pointer coordinate (0..65535 on the droppix monitor) into desktop pixels.
// Extracted verbatim from right_click's original body so all aux-device injectors
// (right_click, scroll, mouse_button) place the pointer identically.
int InputInjector::scale_x(uint16_t x_norm) const {
  int px = outX_ + static_cast<int>(static_cast<int64_t>(x_norm) * outW_ / 65535);
  return std::clamp(px, 0, deskW_ - 1);
}
int InputInjector::scale_y(uint16_t y_norm) const {
  int py = outY_ + static_cast<int>(static_cast<int64_t>(y_norm) * outH_ / 65535);
  return std::clamp(py, 0, deskH_ - 1);
}

void InputInjector::right_click(uint16_t x_norm, uint16_t y_norm) {
  if (rc_fd_ < 0 || outW_ <= 0 || outH_ <= 0) return;
  emit(rc_fd_, EV_ABS, ABS_X, scale_x(x_norm));
  emit(rc_fd_, EV_ABS, ABS_Y, scale_y(y_norm));
  emit(rc_fd_, EV_SYN, SYN_REPORT, 0);
  emit(rc_fd_, EV_KEY, BTN_RIGHT, 1);
  emit(rc_fd_, EV_SYN, SYN_REPORT, 0);
  emit(rc_fd_, EV_KEY, BTN_RIGHT, 0);
  emit(rc_fd_, EV_SYN, SYN_REPORT, 0);
}

void InputInjector::scroll(int dx, int dy, uint16_t x_norm, uint16_t y_norm) {
  if (rc_fd_ < 0) return;
  emit(rc_fd_, EV_ABS, ABS_X, scale_x(x_norm));
  emit(rc_fd_, EV_ABS, ABS_Y, scale_y(y_norm));
  if (dx) emit(rc_fd_, EV_REL, REL_HWHEEL, dx);
  if (dy) emit(rc_fd_, EV_REL, REL_WHEEL, dy);
  emit(rc_fd_, EV_SYN, SYN_REPORT, 0);
}

void InputInjector::mouse_button(uint8_t button, bool down, uint16_t x_norm, uint16_t y_norm) {
  if (rc_fd_ < 0) return;
  int code = (button == 2) ? BTN_MIDDLE : BTN_RIGHT;   // 1=right, 2=middle
  emit(rc_fd_, EV_ABS, ABS_X, scale_x(x_norm));
  emit(rc_fd_, EV_ABS, ABS_Y, scale_y(y_norm));
  emit(rc_fd_, EV_KEY, code, down ? 1 : 0);
  emit(rc_fd_, EV_SYN, SYN_REPORT, 0);
}

InputInjector::~InputInjector() {
  if (fd_ >= 0) {
    // Lift any fingers still down BEFORE destroying the device: Xorg segfaults if a
    // touch device vanishes mid-contact (its touch bookkeeping is freed under the
    // active sequence). This fires on every session teardown — orientation restarts
    // routinely happen while a finger is on the glass. Release directly (not via
    // inject()) so the two-finger-tap detector can't fire a spurious right-click.
    const MtSlots::Update u = slots_.update({});
    const bool wasDown = !u.lifted.empty() || anyDown_;
    for (int slot : u.lifted) {
      emit(fd_, EV_ABS, ABS_MT_SLOT, slot);
      emit(fd_, EV_ABS, ABS_MT_TRACKING_ID, -1);
    }
    if (anyDown_) emit(fd_, EV_KEY, BTN_TOUCH, 0);
    emit(fd_, EV_SYN, SYN_REPORT, 0);
    if (wasDown) {   // give the server a beat to drain the release before the node vanishes
      timespec ts{0, 50 * 1000 * 1000};
      nanosleep(&ts, nullptr);
    }
    ioctl(fd_, UI_DEV_DESTROY); ::close(fd_);
  }
  if (rc_fd_ >= 0) { ioctl(rc_fd_, UI_DEV_DESTROY); ::close(rc_fd_); }
}
}  // namespace droppix
