#include <gtest/gtest.h>
#include "monitor_geometry.h"
using namespace droppix;

static const char* kSample =
  "Output: 1 HDMI-A-3 333136d1\n"
  "\tenabled\n"
  "\tGeometry: 0,0 1600x900\n"
  "Output: 2 DP-2 fef981ef\n"
  "\tenabled\n"
  "\tGeometry: 1600,0 1920x1080\n"
  "Output: 3 Unknown-1 abcd\n"
  "\tenabled\n"
  "\tGeometry: 3520,0 2560x1600\n";

TEST(MonitorGeometry, ParsesOutputs) {
  auto outs = parse_kscreen_outputs(kSample);
  ASSERT_EQ(outs.size(), 3u);
  EXPECT_EQ(outs[0].name, "HDMI-A-3");
  EXPECT_TRUE(outs[2].enabled);
  EXPECT_EQ(outs[2].geom.x, 3520);
  EXPECT_EQ(outs[2].geom.w, 2560);
  EXPECT_EQ(outs[2].geom.h, 1600);
}
TEST(MonitorGeometry, DesktopBounds) {
  Rect b = desktop_bounds(parse_kscreen_outputs(kSample));
  EXPECT_EQ(b.w, 3520 + 2560);   // 6080
  EXPECT_EQ(b.h, 1600);
}
TEST(MonitorGeometry, SelectsDroppixBySizeMatch) {
  Rect r;
  ASSERT_TRUE(select_droppix(parse_kscreen_outputs(kSample), 2560, 1600, r));
  EXPECT_EQ(r.x, 3520); EXPECT_EQ(r.w, 2560); EXPECT_EQ(r.h, 1600);
}
TEST(MonitorGeometry, SelectFailsWhenNoMatch) {
  Rect r;
  EXPECT_FALSE(select_droppix(parse_kscreen_outputs(kSample), 1234, 5678, r));
}
