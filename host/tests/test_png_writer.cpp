#include <gtest/gtest.h>
#include "png_writer.h"

TEST(PngWriter, BgraToRgbaSwapsRedAndBlueAndForcesAlpha) {
  // One pixel: B=0x10, G=0x20, R=0x30, X=0x00
  std::vector<unsigned char> bgra = {0x10, 0x20, 0x30, 0x00};
  auto rgba = droppix::bgra_to_rgba(bgra);
  ASSERT_EQ(rgba.size(), 4u);
  EXPECT_EQ(rgba[0], 0x30);  // R
  EXPECT_EQ(rgba[1], 0x20);  // G
  EXPECT_EQ(rgba[2], 0x10);  // B
  EXPECT_EQ(rgba[3], 0xFF);  // alpha forced opaque
}
