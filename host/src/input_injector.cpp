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

bool InputInjector::open(const Rect& monitor, int desktop_w, int desktop_h) {
  monitor_ = monitor; desktop_w_ = desktop_w; desktop_h_ = desktop_h;
  fd_ = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (fd_ < 0) { std::fprintf(stderr, "uinput open failed (need root); input disabled\n"); return false; }

  ioctl(fd_, UI_SET_EVBIT, EV_KEY);
  ioctl(fd_, UI_SET_KEYBIT, BTN_LEFT);
  ioctl(fd_, UI_SET_EVBIT, EV_ABS);
  ioctl(fd_, UI_SET_ABSBIT, ABS_X);
  ioctl(fd_, UI_SET_ABSBIT, ABS_Y);

  uinput_abs_setup ax{}; ax.code = ABS_X; ax.absinfo.minimum = 0; ax.absinfo.maximum = 65535;
  ioctl(fd_, UI_ABS_SETUP, &ax);
  uinput_abs_setup ay{}; ay.code = ABS_Y; ay.absinfo.minimum = 0; ay.absinfo.maximum = 65535;
  ioctl(fd_, UI_ABS_SETUP, &ay);

  uinput_setup us{};
  us.id.bustype = BUS_USB; us.id.vendor = 0x1209; us.id.product = 0xd701;
  std::strncpy(us.name, "droppix-touch", sizeof(us.name) - 1);
  if (ioctl(fd_, UI_DEV_SETUP, &us) < 0 || ioctl(fd_, UI_DEV_CREATE) < 0) {
    std::fprintf(stderr, "uinput device create failed; input disabled\n");
    ::close(fd_); fd_ = -1; return false;
  }
  return true;
}

void InputInjector::inject(uint8_t action, uint16_t x_norm, uint16_t y_norm) {
  if (fd_ < 0) return;
  AbsCoord c = map_to_abs(x_norm, y_norm, monitor_, desktop_w_, desktop_h_);
  emit(fd_, EV_ABS, ABS_X, c.x);
  emit(fd_, EV_ABS, ABS_Y, c.y);
  if (action == 0) emit(fd_, EV_KEY, BTN_LEFT, 1);       // down
  else if (action == 2) emit(fd_, EV_KEY, BTN_LEFT, 0);  // up
  emit(fd_, EV_SYN, SYN_REPORT, 0);
}

InputInjector::~InputInjector() {
  if (fd_ >= 0) { ioctl(fd_, UI_DEV_DESTROY); ::close(fd_); }
}
}  // namespace droppix
