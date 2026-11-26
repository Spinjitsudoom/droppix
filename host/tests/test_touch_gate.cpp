#include <gtest/gtest.h>
#include "touch_gate.h"

using namespace droppix;
using Frames = std::vector<TouchGate::Frame>;

// Qt turns a touch into a mouse press and fakes a release even when the touch is cancelled, so
// the only way a two-finger tap clicks nothing is for its touches never to reach the desktop.
// The gate holds a new touch back until it is clearly not a two-finger tap.

namespace {
TouchContact at(uint8_t id, uint16_t x, uint16_t y) { return {id, x, y, 500}; }
}

TEST(TouchGate, ASingleTapIsDeliveredWholeWhenItLifts) {
  TouchGate g;
  EXPECT_TRUE(g.update({at(0, 1000, 2000)}, 0).empty());
  Frames f = g.update({}, 60);
  ASSERT_EQ(f.size(), 2u);
  ASSERT_EQ(f[0].size(), 1u);
  EXPECT_EQ(f[0][0].x, 1000); EXPECT_EQ(f[0][0].y, 2000);
  EXPECT_TRUE(f[1].empty());   // the lift
}

TEST(TouchGate, ATwoFingerTapIsNeverDelivered) {
  TouchGate g;
  EXPECT_TRUE(g.update({at(0, 1000, 2000)}, 0).empty());
  EXPECT_TRUE(g.update({at(0, 1000, 2000), at(1, 3000, 2000)}, 30).empty());
  EXPECT_TRUE(g.update({at(1, 3000, 2000)}, 80).empty());
  EXPECT_TRUE(g.update({}, 100).empty());
  EXPECT_TRUE(g.tick(1000).empty());
}

TEST(TouchGate, ADragStartsFromWhereTheFingerWentDown) {
  // Starting it where the finger is once it has moved would grab whatever is ~4% away.
  TouchGate g;
  g.update({at(0, 1000, 1000)}, 0);
  Frames f = g.update({at(0, 1000, 5000)}, 30);
  ASSERT_EQ(f.size(), 2u);
  EXPECT_EQ(f[0][0].y, 1000);
  EXPECT_EQ(f[1][0].y, 5000);
  f = g.update({at(0, 1000, 6000)}, 40);
  ASSERT_EQ(f.size(), 1u);
  EXPECT_EQ(f[0][0].y, 6000);
  f = g.update({}, 50);
  ASSERT_EQ(f.size(), 1u);
  EXPECT_TRUE(f[0].empty());
}

TEST(TouchGate, AStillFingerIsReleasedAfterTheHold) {
  TouchGate g;
  g.update({at(0, 1000, 1000)}, 0);
  EXPECT_TRUE(g.tick(TouchGate::kHoldMs - 10).empty());
  Frames f = g.tick(TouchGate::kHoldMs + 1);
  ASSERT_FALSE(f.empty());
  EXPECT_EQ(f.back().size(), 1u);
  EXPECT_EQ(g.update({at(0, 1000, 1000)}, 200).size(), 1u);   // now passing through
}

TEST(TouchGate, TwoStillFingersWaitOutTheTapTime) {
  TouchGate g;
  g.update({at(0, 1000, 1000), at(1, 3000, 1000)}, 0);
  EXPECT_TRUE(g.tick(TouchGate::kHoldMs + 50).empty());
  Frames f = g.tick(TwoFingerTap::kTapMs + 1);
  ASSERT_FALSE(f.empty());
  EXPECT_EQ(f.back().size(), 2u);
}

TEST(TouchGate, APinchIsReleasedAsSoonAsItMoves) {
  TouchGate g;
  g.update({at(0, 1000, 1000), at(1, 3000, 1000)}, 0);
  Frames f = g.update({at(0, 1000, 1000), at(1, 9000, 1000)}, 40);
  ASSERT_FALSE(f.empty());
  EXPECT_EQ(f.back().size(), 2u);
  EXPECT_EQ(f.back()[1].x, 9000);
}

TEST(TouchGate, ResetForgetsAHeldTouch) {
  TouchGate g;
  g.update({at(0, 1000, 1000)}, 0);
  g.reset();
  EXPECT_TRUE(g.tick(1000).empty());
  EXPECT_TRUE(g.update({}, 1001).empty());
}

TEST(TouchGate, EveryFingerOfAMovingGestureStartsWhereItWentDown) {
  // Android reports a second finger in its own event, so the first frame held only one.
  TouchGate g;
  g.update({at(0, 1000, 1000)}, 0);
  g.update({at(0, 1000, 1000), at(1, 3000, 1000)}, 10);
  Frames f = g.update({at(0, 1000, 6000), at(1, 3000, 6000)}, 40);   // a two-finger scroll
  ASSERT_GE(f.size(), 2u);
  const auto& start = f[f.size() - 2];
  ASSERT_EQ(start.size(), 2u);
  EXPECT_EQ(start[0].y, 1000);
  EXPECT_EQ(start[1].y, 1000);   // was 6000: the second finger began already moved
  EXPECT_EQ(f.back()[1].y, 6000);
}
