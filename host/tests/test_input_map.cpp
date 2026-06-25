#include <gtest/gtest.h>
#include "input_map.h"
using namespace droppix;

TEST(InputMap, SingleMonitorAtOriginIsIdentity) {
  // monitor == desktop -> abs == input norm (within rounding)
  AbsCoord c = map_to_abs(30000, 40000, Rect{0,0,1920,1080}, 1920, 1080);
  EXPECT_EQ(c.x, 30000);
  EXPECT_EQ(c.y, 40000);
}
TEST(InputMap, OffsetMonitorMapsIntoDesktop) {
  // droppix monitor on the right half of a 3840x1080 desktop
  Rect mon{1920,0,1920,1080};
  EXPECT_EQ(map_to_abs(0,     0, mon, 3840, 1080).x, 32768);  // left edge of monitor
  EXPECT_EQ(map_to_abs(65535, 0, mon, 3840, 1080).x, 65535);  // right edge of desktop
  EXPECT_EQ(map_to_abs(0,     0, mon, 3840, 1080).y, 0);
}
