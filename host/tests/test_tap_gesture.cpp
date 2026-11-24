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

// A real mouse spaces move -> press -> release; an instant press+release from a pointer
// that just teleported confused Plasma. The steps come due off the stream loop's clock.
TEST(ClickSchedule, SpacesMovePressAndRelease) {
  ClickSchedule s;
  s.start(1000, 111, 222);
  auto a = s.due(1000);
  ASSERT_EQ(a.size(), 2u); EXPECT_EQ(a[1], ClickSchedule::Step::Move);
  EXPECT_TRUE(s.due(1029).empty());
  auto b = s.due(1030);
  ASSERT_EQ(b.size(), 1u); EXPECT_EQ(b[0], ClickSchedule::Step::Press);
  EXPECT_TRUE(s.due(1109).empty());
  auto c = s.due(1110);
  ASSERT_EQ(c.size(), 1u); EXPECT_EQ(c[0], ClickSchedule::Step::Release);
  EXPECT_FALSE(s.active());
  EXPECT_EQ(s.x(), 111); EXPECT_EQ(s.y(), 222);
}
TEST(ClickSchedule, LateTickDeliversEveryOverdueStepInOrder) {
  ClickSchedule s;
  s.start(0, 1, 2);
  auto all = s.due(500);
  ASSERT_EQ(all.size(), 4u);
  EXPECT_EQ(all[0], ClickSchedule::Step::Wiggle);
  EXPECT_EQ(all[1], ClickSchedule::Step::Move);
  EXPECT_EQ(all[2], ClickSchedule::Step::Press);
  EXPECT_EQ(all[3], ClickSchedule::Step::Release);
}
TEST(ClickSchedule, IdleHasNothingDue) {
  ClickSchedule s;
  EXPECT_FALSE(s.active());
  EXPECT_TRUE(s.due(123456).empty());
}

// The kernel drops an ABS value equal to the last one sent. Tapping the same desktop pixel
// twice (with the real mouse moved in between) made the Move step a no-op and the press
// landed at the mouse. A Wiggle (1 px off) always precedes Move so the position changes.
TEST(ClickSchedule, WiggleAlwaysPrecedesMove) {
  ClickSchedule s;
  s.start(0, 5, 6);
  auto a = s.due(0);
  ASSERT_EQ(a.size(), 2u);
  EXPECT_EQ(a[0], ClickSchedule::Step::Wiggle);
  EXPECT_EQ(a[1], ClickSchedule::Step::Move);
}
