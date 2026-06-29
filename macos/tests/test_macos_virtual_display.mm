#include <gtest/gtest.h>
#include "macos_virtual_display.h"

// Smoke test for the CGVirtualDisplay wrapper: create a real virtual
// display, confirm it gets assigned a display ID, and tear it down cleanly.
// Requires Screen Recording permission to be pre-granted in CI/headless runs;
// open() returning false (rather than crashing) is itself a meaningful result
// there, so this only asserts on the no-crash / id-validity contract.
TEST(MacVirtualDisplay, OpenAssignsDisplayIdAndCloseIsSafe) {
  droppix::MacVirtualDisplay vd;
  EXPECT_EQ(vd.display_id(), kCGNullDirectDisplay);
  bool opened = vd.open(640, 480, 60);
  if (opened) {
    EXPECT_NE(vd.display_id(), kCGNullDirectDisplay);
  }
  vd.close();
  EXPECT_EQ(vd.display_id(), kCGNullDirectDisplay);
}

TEST(MacVirtualDisplay, CloseWithoutOpenIsSafeNoOp) {
  droppix::MacVirtualDisplay vd;
  vd.close();  // must not crash
  EXPECT_EQ(vd.display_id(), kCGNullDirectDisplay);
}
