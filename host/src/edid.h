#pragma once
#include <cstdint>
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

// Build a 128-byte EDID 1.3 block encoding `t` as Detailed Timing #1. The final checksum
// byte makes the whole block sum to 0 (mod 256). `serial` goes in the serial-number field
// (bytes 12-15) and MUST be unique per evdi output — KWin/kscreen deduplicate monitors with
// identical EDID identity (only one output then appears; the rest never composite). droppix
// passes each session's port.
std::vector<unsigned char> build_edid(const Timing& t, uint32_t serial = 0);

}  // namespace droppix
