#include "input_injector.h"
#include <linux/uinput.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstring>
#include <cstdio>

namespace droppix {
namespace {
void emit(int fd, int type, int code, int val) {
  input_event ev{};
  ev.type = type; ev.code = code; ev.value = val;
  ssize_t n = ::write(fd, &ev, sizeof(ev));
  (void)n;
}
}  // namespace

bool InputInjector::open() {
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
  std::strncpy(us.name, "droppix-touch", sizeof(us.name) - 1);
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
}

InputInjector::~InputInjector() {
  if (fd_ >= 0) { ioctl(fd_, UI_DEV_DESTROY); ::close(fd_); }
}
}  // namespace droppix
