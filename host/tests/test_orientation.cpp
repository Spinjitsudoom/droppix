#include <gtest/gtest.h>
#include "orientation.h"
using namespace droppix;

TEST(Orientation, PortraitCodes) {
  EXPECT_FALSE(orientation_is_portrait(0));   // 0°   landscape
  EXPECT_TRUE(orientation_is_portrait(1));    // 90°  portrait
  EXPECT_FALSE(orientation_is_portrait(2));   // 180° landscape (flipped)
  EXPECT_TRUE(orientation_is_portrait(3));    // 270° portrait (flipped)
}
