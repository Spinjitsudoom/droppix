#include "edid.h"
#include <array>

namespace droppix {

Timing timing_1080p60() {
  return Timing{
      /*pixel_clock_khz*/ 148500,
      /*h_active*/ 1920, /*h_front*/ 88,  /*h_sync*/ 44, /*h_blank*/ 280,
      /*v_active*/ 1080, /*v_front*/ 4,   /*v_sync*/ 5,  /*v_blank*/ 45,
      /*h_mm*/ 480,      /*v_mm*/ 270};
}

static void write_dtd(unsigned char* d, const Timing& t) {
  const int clk = t.pixel_clock_khz / 10;  // 10 kHz units
  d[0] = clk & 0xFF;
  d[1] = (clk >> 8) & 0xFF;

  d[2] = t.h_active & 0xFF;
  d[3] = t.h_blank & 0xFF;
  d[4] = ((t.h_active >> 8) & 0x0F) << 4 | ((t.h_blank >> 8) & 0x0F);

  d[5] = t.v_active & 0xFF;
  d[6] = t.v_blank & 0xFF;
  d[7] = ((t.v_active >> 8) & 0x0F) << 4 | ((t.v_blank >> 8) & 0x0F);

  d[8] = t.h_front & 0xFF;
  d[9] = t.h_sync & 0xFF;
  d[10] = ((t.v_front & 0x0F) << 4) | (t.v_sync & 0x0F);
  d[11] = ((t.h_front >> 8) & 0x03) << 6 |
          ((t.h_sync  >> 8) & 0x03) << 4 |
          ((t.v_front >> 4) & 0x03) << 2 |
          ((t.v_sync  >> 4) & 0x03);

  d[12] = t.h_mm & 0xFF;
  d[13] = t.v_mm & 0xFF;
  d[14] = ((t.h_mm >> 8) & 0x0F) << 4 | ((t.v_mm >> 8) & 0x0F);
  d[15] = 0;  // h border
  d[16] = 0;  // v border
  d[17] = 0x1E;  // digital separate sync, +h +v
}

std::vector<unsigned char> build_edid(const Timing& t) {
  std::array<unsigned char, 128> e{};  // zero-initialised

  // Header (bytes 0-7).
  const unsigned char header[8] = {0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00};
  for (int i = 0; i < 8; ++i) e[i] = header[i];

  // Manufacturer ID "DPX" (bytes 8-9), 5-bit packed big-endian.
  // 'D'=4,'P'=16,'X'=24 -> (4<<10)|(16<<5)|24 = 0x1218.
  e[8]  = 0x12;
  e[9]  = 0x18;
  // Product code (10-11), serial (12-15) left as 0/defaults.
  e[10] = 0x01; e[11] = 0x00;
  e[16] = 0x01;  // week
  e[17] = 0x21;  // year 2023 (1990 + 0x21)

  e[18] = 0x01;  // EDID version 1
  e[19] = 0x03;  // revision 3

  // Basic display params (byte 20: digital input).
  e[20] = 0x80;            // digital
  e[21] = t.h_mm / 10;     // max horizontal image size (cm)
  e[22] = t.v_mm / 10;     // max vertical image size (cm)
  e[23] = 0x78;            // gamma 2.2
  e[24] = 0x0A;            // RGB, preferred timing = DTD#1

  // Chromaticity (25-34): generic sRGB-ish values.
  const unsigned char chroma[10] = {
      0xEE,0x91,0xA3,0x54,0x4C,0x99,0x26,0x0F,0x50,0x54};
  for (int i = 0; i < 10; ++i) e[25 + i] = chroma[i];

  // Established/standard timings (35-53): none required, leave zero/unused.
  for (int i = 38; i <= 53; ++i) e[i] = 0x01;  // unused standard timing markers

  // Detailed Timing Descriptor #1 (bytes 54-71).
  write_dtd(&e[54], t);

  // Descriptor #2 (72-89): monitor name "droppix".
  e[72] = 0; e[73] = 0; e[74] = 0; e[75] = 0xFC; e[76] = 0;
  const char* name = "droppix";
  int p = 77;
  for (const char* c = name; *c && p < 90; ++c) e[p++] = *c;
  if (p < 90) e[p++] = 0x0A;     // line-feed terminator after the name
  while (p < 90) e[p++] = 0x20;  // pad remainder with spaces

  // Descriptors #3 (90-107) and #4 (108-125): dummy display descriptors.
  // Bytes 0-2 of each (90-92 / 108-110) stay 0x000000 to mark "not a DTD";
  // the descriptor tag (0x10 = dummy) goes at byte 3 (=93 / 111).
  e[93] = e[111] = 0x10;

  e[126] = 0;  // extension count

  // Checksum (byte 127): make the block sum to 0 mod 256.
  int sum = 0;
  for (int i = 0; i < 127; ++i) sum += e[i];
  e[127] = static_cast<unsigned char>((256 - (sum % 256)) % 256);

  return std::vector<unsigned char>(e.begin(), e.end());
}

}  // namespace droppix
