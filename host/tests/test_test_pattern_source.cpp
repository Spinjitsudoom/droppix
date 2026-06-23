#include <gtest/gtest.h>
#include "test_pattern_source.h"

using namespace droppix;

TEST(TestPatternSource, ProducesCorrectlySizedValidFrames) {
  TestPatternSource s(160, 120, 30);
  int w = 0, h = 0;
  ASSERT_TRUE(s.start(w, h));
  EXPECT_EQ(w, 160); EXPECT_EQ(h, 120);
  Frame f = s.next(0);
  ASSERT_TRUE(f.valid);
  EXPECT_EQ(f.width, 160); EXPECT_EQ(f.height, 120);
  EXPECT_EQ(f.stride, 160 * 4);
  EXPECT_EQ(f.bgra.size(), size_t(160) * 120 * 4);
}

TEST(TestPatternSource, ContentChangesBetweenFrames) {
  TestPatternSource s(160, 120, 30);
  int w, h; s.start(w, h);
  Frame a = s.next(0);
  Frame b = s.next(0);
  EXPECT_NE(a.bgra, b.bgra);  // pattern animates
}
