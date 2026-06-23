#pragma once
#include <vector>

namespace droppix {

struct Timing {
  int pixel_clock_khz;            // e.g. 148500
  int h_active, h_front, h_sync, h_blank;  // pixels
  int v_active, v_front, v_sync, v_blank;  // lines
  int h_mm, v_mm;                 // physical image size in millimetres
};

// CEA-861 1920x1080 @ 60 Hz.
Timing timing_1080p60();

// Build a 128-byte EDID 1.3 block encoding `t` as Detailed Timing #1.
// The final checksum byte makes the whole block sum to 0 (mod 256).
std::vector<unsigned char> build_edid(const Timing& t);

}  // namespace droppix
