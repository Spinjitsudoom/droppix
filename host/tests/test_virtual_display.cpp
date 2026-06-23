#include <gtest/gtest.h>
#include "virtual_display.h"
#include "edid.h"

// connect() on a VirtualDisplay that was never opened must be a safe no-op
// (no evdi call on a null handle); construction/destruction must not crash.
TEST(VirtualDisplay, ConnectWithoutOpenIsSafeNoOp) {
  droppix::VirtualDisplay vd;
  EXPECT_EQ(vd.node(), -1);
  EXPECT_EQ(vd.handle(), nullptr);
  vd.connect(droppix::build_edid(droppix::timing_1080p60()));  // must not crash
  EXPECT_EQ(vd.handle(), nullptr);  // still no handle, no connection happened
}
