#include <gtest/gtest.h>
#include <numeric>
#include <string>
#include "edid.h"

using droppix::build_edid;
using droppix::timing_1080p60;

TEST(Edid, IsExactly128Bytes) {
  EXPECT_EQ(build_edid(timing_1080p60()).size(), 128u);
}

TEST(Edid, HasFixedHeaderPattern) {
  auto e = build_edid(timing_1080p60());
  const unsigned char header[8] = {0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00};
  for (int i = 0; i < 8; ++i) EXPECT_EQ(e[i], header[i]) << "byte " << i;
}

TEST(Edid, ChecksumMakesBlockSumZeroMod256) {
  auto e = build_edid(timing_1080p60());
  int sum = std::accumulate(e.begin(), e.end(), 0);
  EXPECT_EQ(sum % 256, 0);
}

TEST(Edid, Version1Point3) {
  auto e = build_edid(timing_1080p60());
  EXPECT_EQ(e[18], 0x01);  // EDID version
  EXPECT_EQ(e[19], 0x03);  // revision 3
}

TEST(Edid, FirstDetailedTimingEncodesActivePixels) {
  auto e = build_edid(timing_1080p60());
  // Detailed Timing Descriptor #1 starts at byte 54.
  const int o = 54;
  int h_active = e[o+2] | ((e[o+4] & 0xF0) << 4);
  int v_active = e[o+5] | ((e[o+7] & 0xF0) << 4);
  EXPECT_EQ(h_active, 1920);
  EXPECT_EQ(v_active, 1080);
}

TEST(Edid, PixelClockEncodedLittleEndianIn10kHzUnits) {
  auto e = build_edid(timing_1080p60());
  const int o = 54;
  int clk = e[o] | (e[o+1] << 8);   // units of 10 kHz
  EXPECT_EQ(clk, 14850);            // 148.5 MHz
}

TEST(Edid, ManufacturerIdEncodesDPX) {
  auto e = build_edid(timing_1080p60());
  EXPECT_EQ(e[8], 0x12);
  EXPECT_EQ(e[9], 0x18);
}

TEST(Edid, DummyDescriptorsAreNotMisreadAsTimings) {
  auto e = build_edid(timing_1080p60());
  // Descriptors #3 (byte 90) and #4 (byte 108): first two bytes must be 0
  // so parsers treat them as display descriptors, not detailed timings.
  EXPECT_EQ(e[90], 0x00); EXPECT_EQ(e[91], 0x00);
  EXPECT_EQ(e[108], 0x00); EXPECT_EQ(e[109], 0x00);
  EXPECT_EQ(e[93], 0x10);   // dummy descriptor tag at offset+3
  EXPECT_EQ(e[111], 0x10);
}

TEST(Edid, MonitorNameDescriptorHasNameAndTerminator) {
  auto e = build_edid(timing_1080p60());
  EXPECT_EQ(e[75], 0xFC);   // monitor name tag
  std::string name(reinterpret_cast<const char*>(&e[77]), 7);
  EXPECT_EQ(name, "droppix");
  EXPECT_EQ(e[84], 0x0A);   // LF terminator right after the 7-char name
}
