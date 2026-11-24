#include "tap_gesture.h"
#include <gtest/gtest.h>

using namespace droppix;

TEST(TwoFingerTap, FiresRightClickAtMidpointOnQuietTwoFingerTap) {
  TwoFingerTap g;
  g.update({{0, 1000, 2000, 0}, {1, 3000, 4000, 0}}, 0);   // both fingers down
  auto r = g.update({}, 100);                               // both up, 100 ms later
  EXPECT_TRUE(r.rightClick);
  EXPECT_EQ(r.x, 2000);   // (1000 + 3000) / 2
  EXPECT_EQ(r.y, 3000);   // (2000 + 4000) / 2
}

TEST(TwoFingerTap, SingleFingerDoesNotClick) {
  TwoFingerTap g;
  g.update({{0, 1000, 2000, 0}}, 0);
  EXPECT_FALSE(g.update({}, 50).rightClick);
}

TEST(TwoFingerTap, MovementIsScrollNotClick) {
  TwoFingerTap g;
  g.update({{0, 1000, 2000, 0}, {1, 3000, 4000, 0}}, 0);
  g.update({{0, 1000, 9000, 0}, {1, 3000, 11000, 0}}, 50);   // moved > threshold
  EXPECT_FALSE(g.update({}, 100).rightClick);
}

TEST(TwoFingerTap, TooSlowIsNotATap) {
  TwoFingerTap g;
  g.update({{0, 1000, 2000, 0}, {1, 3000, 4000, 0}}, 0);
  EXPECT_FALSE(g.update({}, 1000).rightClick);   // 1 s > 400 ms
}

TEST(TwoFingerTap, ThreeFingersDoNotClick) {
  TwoFingerTap g;
  g.update({{0, 1, 1, 0}, {1, 2, 2, 0}, {2, 3, 3, 0}}, 0);
  EXPECT_FALSE(g.update({}, 50).rightClick);
}

TEST(TwoFingerTap, StaggeredTwoFingerStillTaps) {
  TwoFingerTap g;
  g.update({{0, 1000, 2000, 0}}, 0);                        // finger 1
  g.update({{0, 1000, 2000, 0}, {1, 3000, 4000, 0}}, 20);   // finger 2 -> count 2
  g.update({{0, 1000, 2000, 0}}, 40);                       // finger 2 up -> count 1
  auto r = g.update({}, 60);                                // finger 1 up
  EXPECT_TRUE(r.rightClick);
  EXPECT_EQ(r.x, 2000); EXPECT_EQ(r.y, 3000);
}

TEST(HoldGesture, StillFingerFiresAfterHoldTimeAtFingerPosition) {
  HoldGesture g;
  EXPECT_FALSE(g.update({{3, 10000, 20000, 0}}, 0).fire);
  EXPECT_FALSE(g.tick(599).fire);
  auto r = g.tick(600);
  EXPECT_TRUE(r.fire); EXPECT_EQ(r.id, 3); EXPECT_EQ(r.x, 10000); EXPECT_EQ(r.y, 20000);
  EXPECT_TRUE(g.spent());
}
TEST(HoldGesture, SmallJitterStillFires) {
  HoldGesture g;
  g.update({{0, 10000, 20000, 0}}, 0);
  g.update({{0, 10900, 19100, 0}}, 300);                  // within kHoldSlop
  auto r = g.tick(650);
  EXPECT_TRUE(r.fire); EXPECT_EQ(r.x, 10900); EXPECT_EQ(r.y, 19100);
}
TEST(HoldGesture, MovementBeyondSlopNeverFires) {
  HoldGesture g;
  g.update({{0, 10000, 20000, 0}}, 0);
  g.update({{0, 11001, 20000, 0}}, 100);
  g.update({{0, 10000, 20000, 0}}, 200);                  // coming back doesn't re-arm
  EXPECT_FALSE(g.tick(1000).fire);
}
TEST(HoldGesture, SecondFingerCancelsHold) {
  HoldGesture g;
  g.update({{0, 10000, 20000, 0}}, 0);
  g.update({{0, 10000, 20000, 0}, {1, 30000, 20000, 0}}, 100);
  g.update({{0, 10000, 20000, 0}}, 200);
  EXPECT_FALSE(g.tick(1000).fire);
}
TEST(HoldGesture, FiresOnlyOncePerSequence) {
  HoldGesture g;
  g.update({{0, 10000, 20000, 0}}, 0);
  EXPECT_TRUE(g.tick(600).fire);
  EXPECT_FALSE(g.tick(2000).fire);
  EXPECT_FALSE(g.update({{0, 40000, 20000, 0}}, 2100).fire);
}
TEST(HoldGesture, LiftResetsForTheNextSequence) {
  HoldGesture g;
  g.update({{0, 10000, 20000, 0}}, 0);
  g.tick(600);
  g.update({}, 700);
  EXPECT_FALSE(g.spent());
  g.update({{1, 5000, 5000, 0}}, 1000);
  EXPECT_TRUE(g.tick(1600).fire);
}
TEST(HoldGesture, QuickTapNeverFires) {
  HoldGesture g;
  g.update({{0, 10000, 20000, 0}}, 0);
  g.update({}, 120);
  EXPECT_FALSE(g.tick(1000).fire);
}
