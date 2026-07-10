#include "touch_normalize.h"
#include <gtest/gtest.h>

using namespace droppix;

TEST(TouchNormalize, OriginMapsToZero) {
  auto c = normalize_touch(0, 0, 1920, 1080, 1.0, 0);
  EXPECT_EQ(c.x, 0);
  EXPECT_EQ(c.y, 0);
}

TEST(TouchNormalize, BottomRightMapsToMax) {
  // The exact edge pixel (w-1,h-1) should map very close to full-scale, not overflow.
  auto c = normalize_touch(1919, 1079, 1920, 1080, 1.0, 0);
  EXPECT_GT(c.x, 65000);
  EXPECT_GT(c.y, 65000);
}

TEST(TouchNormalize, MidpointMapsToHalfScale) {
  auto c = normalize_touch(960, 540, 1920, 1080, 1.0, 0);
  EXPECT_NEAR(c.x, 32767, 200);
  EXPECT_NEAR(c.y, 32767, 200);
}

TEST(TouchNormalize, PressureScalesTo1023) {
  EXPECT_EQ(normalize_touch(0, 0, 100, 100, 0.0, 0).pressure, 0);
  EXPECT_EQ(normalize_touch(0, 0, 100, 100, 1.0, 0).pressure, 1023);
  EXPECT_NEAR(normalize_touch(0, 0, 100, 100, 0.5, 0).pressure, 511, 2);
}

TEST(TouchNormalize, OutOfBoundsCoordinatesClamp) {
  // Negative or beyond-widget positions (drag off the edge) must clamp, not wrap/overflow.
  auto neg = normalize_touch(-50, -50, 1920, 1080, 1.0, 0);
  EXPECT_EQ(neg.x, 0);
  EXPECT_EQ(neg.y, 0);
  auto over = normalize_touch(5000, 5000, 1920, 1080, 1.0, 0);
  EXPECT_EQ(over.x, 65535);
  EXPECT_EQ(over.y, 65535);
}

TEST(TouchNormalize, ZeroSizeWidgetDoesNotDivideByZero) {
  // A widget mid-layout can briefly report 0 size; must not crash or produce NaN/UB.
  auto c = normalize_touch(0, 0, 0, 0, 1.0, 0);
  EXPECT_EQ(c.x, 0);
  EXPECT_EQ(c.y, 0);
}

TEST(TouchNormalize, IdIsPreservedForMultiTouch) {
  EXPECT_EQ(normalize_touch(0, 0, 100, 100, 1.0, 7).id, 7);
}
