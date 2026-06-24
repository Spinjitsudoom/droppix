#include "cvt.h"
#include <cmath>

namespace droppix {
namespace {
constexpr int kCell = 8;
constexpr double kClockStepKhz = 250.0;     // 0.25 MHz
constexpr double kRbMinVBlankUs = 460.0;
constexpr int kRbHBlank = 160, kRbHFront = 48, kRbHSync = 32, kRbVFront = 3;

int vsync_for(int w, int h) {
  const double ar = static_cast<double>(w) / static_cast<double>(h);
  const struct { double ar; int vs; } table[] = {
    {4.0/3, 4}, {16.0/9, 5}, {16.0/10, 6}, {5.0/4, 7}, {15.0/9, 7}};
  for (const auto& e : table) if (std::abs(ar - e.ar) < 0.02) return e.vs;
  return 10;
}
}  // namespace

Timing cvt_rb_timing(int width, int height, int refresh_hz) {
  const int h_active = (width / kCell) * kCell;
  const int vs = vsync_for(width, height);
  const double hperiod_us =
      (1e6 / refresh_hz - kRbMinVBlankUs) / (height + kRbVFront);
  int vbi = static_cast<int>(std::ceil(kRbMinVBlankUs / hperiod_us));
  const int vbi_min = kRbVFront + vs + 1;
  if (vbi < vbi_min) vbi = vbi_min;
  const int v_total = height + vbi;
  const int h_total = h_active + kRbHBlank;
  const double clk_khz = static_cast<double>(h_total) * v_total * refresh_hz / 1000.0;
  const int clk = static_cast<int>(std::floor(clk_khz / kClockStepKhz) * kClockStepKhz);

  Timing t{};
  t.pixel_clock_khz = clk;
  t.h_active = h_active; t.h_front = kRbHFront; t.h_sync = kRbHSync; t.h_blank = kRbHBlank;
  t.v_active = height;   t.v_front = kRbVFront; t.v_sync = vs;       t.v_blank = vbi;
  t.h_mm = static_cast<int>(std::lround(width * 25.4 / 96.0));   // ~96 DPI physical size
  t.v_mm = static_cast<int>(std::lround(height * 25.4 / 96.0));
  return t;
}

Timing mode_timing(int width, int height, int refresh_hz) {
  if (width == 1920 && height == 1080 && refresh_hz == 60) return timing_1080p60();
  return cvt_rb_timing(width, height, refresh_hz);
}
}  // namespace droppix
