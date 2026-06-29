#include <gtest/gtest.h>
#include "macos_frame_source.h"

// Smoke test: start a MacFrameSource (creates a real virtual display) and
// pull one frame. Skips the frame assertion (not fail) if the platform
// denies display/capture creation, e.g. no Screen Recording permission in a
// headless CI run — the same posture test_virtual_display.cpp takes for
// evdi's "no KWin session" case.
TEST(MacFrameSource, StartAndGrabOneFrame) {
  droppix::MacFrameSource src(640, 480, 60);
  int w = 0, h = 0;
  if (!src.start(w, h)) {
    GTEST_SKIP() << "virtual display/capture unavailable in this environment";
  }
  EXPECT_EQ(w, 640);
  EXPECT_EQ(h, 480);
  EXPECT_NE(src.native_display_id(), -1);

  droppix::Frame f = src.next(5000);
  EXPECT_TRUE(f.valid);
  if (f.valid) {
    EXPECT_EQ(f.width, 640);
    EXPECT_EQ(f.height, 480);
    EXPECT_FALSE(f.bgra.empty());
  }
}
