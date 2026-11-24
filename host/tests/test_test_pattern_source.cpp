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

// A screen mode change mid-session used to hand the encoder a frame whose size no longer
// matched the session (or its own pixel buffer), so it read past the buffer.
namespace {
Frame frame(int w, int h, int stride, size_t bytes) {
  Frame f; f.width = w; f.height = h; f.stride = stride; f.bgra.resize(bytes); f.valid = true;
  return f;
}
}  // namespace
TEST(FrameFitsSession, MatchingFrameFits) {
  EXPECT_TRUE(frame_fits_session(frame(800, 600, 3200, 3200u * 600), 800, 600));
}
TEST(FrameFitsSession, NewModeDoesNotFit) {
  EXPECT_FALSE(frame_fits_session(frame(1024, 768, 4096, 4096u * 768), 800, 600));
}
TEST(FrameFitsSession, BufferSmallerThanItsStatedSizeDoesNotFit) {
  EXPECT_FALSE(frame_fits_session(frame(800, 600, 3200, 3200u * 600 - 1), 800, 600));
  EXPECT_FALSE(frame_fits_session(frame(800, 600, 3000, 3200u * 600), 800, 600));
}
