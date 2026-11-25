#include "input_caps.h"
#include <algorithm>
#include <linux/input-event-codes.h>
#include <gtest/gtest.h>

using namespace droppix;

static bool has(const std::vector<int>& v, int c) { return std::find(v.begin(), v.end(), c) != v.end(); }
static const AbsAxis* axis(const DeviceCaps& d, int code) {
  for (const auto& a : d.abs) if (a.code == code) return &a;
  return nullptr;
}

// libinput refuses a tablet without these ("missing tablet capabilities: btn-stylus
// resolution. Ignoring this device.") — on GNOME and KDE alike, so the stylus never worked.
TEST(PenCaps, MeetsLibinputTabletRequirements) {
  const DeviceCaps d = pen_device_caps();
  EXPECT_TRUE(has(d.keys, BTN_TOOL_PEN));
  EXPECT_TRUE(has(d.keys, BTN_STYLUS));
  EXPECT_TRUE(has(d.keys, BTN_TOUCH));
  const AbsAxis* x = axis(d, ABS_X);
  const AbsAxis* y = axis(d, ABS_Y);
  ASSERT_NE(x, nullptr);
  ASSERT_NE(y, nullptr);
  EXPECT_GT(x->resolution, 0);
  EXPECT_GT(y->resolution, 0);
}
TEST(PenCaps, KeepsTheWireRangesAndEraser) {
  const DeviceCaps d = pen_device_caps();
  EXPECT_TRUE(has(d.keys, BTN_TOOL_RUBBER));
  EXPECT_TRUE(has(d.props, INPUT_PROP_DIRECT));
  EXPECT_EQ(axis(d, ABS_X)->max, 65535);
  EXPECT_EQ(axis(d, ABS_Y)->max, 65535);
  ASSERT_NE(axis(d, ABS_PRESSURE), nullptr);
  EXPECT_EQ(axis(d, ABS_PRESSURE)->max, 1023);
}
