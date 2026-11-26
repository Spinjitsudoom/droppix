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
TEST(ClickSchedule, SpacesLandMovePressAndRelease) {
  ClickSchedule s;
  s.start(1000, 111, 222);
  auto a = s.due(1000);
  ASSERT_EQ(a.size(), 1u); EXPECT_EQ(a[0], ClickSchedule::Step::Land);
  EXPECT_TRUE(s.due(1014).empty());
  auto m = s.due(1015);
  ASSERT_EQ(m.size(), 2u); EXPECT_EQ(m[1], ClickSchedule::Step::Move);
  EXPECT_TRUE(s.due(1044).empty());
  auto b = s.due(1045);
  ASSERT_EQ(b.size(), 1u); EXPECT_EQ(b[0], ClickSchedule::Step::Press);
  EXPECT_TRUE(s.due(1124).empty());
  auto c = s.due(1125);
  ASSERT_EQ(c.size(), 1u); EXPECT_EQ(c[0], ClickSchedule::Step::Release);
  EXPECT_FALSE(s.active());
  EXPECT_EQ(s.x(), 111); EXPECT_EQ(s.y(), 222);
}
TEST(ClickSchedule, LateTickDeliversEveryOverdueStepInOrder) {
  ClickSchedule s;
  s.start(0, 1, 2);
  auto all = s.due(500);
  ASSERT_EQ(all.size(), 5u);
  EXPECT_EQ(all[0], ClickSchedule::Step::Land);
  EXPECT_EQ(all[1], ClickSchedule::Step::Wiggle);
  EXPECT_EQ(all[2], ClickSchedule::Step::Move);
  EXPECT_EQ(all[3], ClickSchedule::Step::Press);
  EXPECT_EQ(all[4], ClickSchedule::Step::Release);
}
// Plasma 6's edge barrier holds the pointer back 100 px when it crosses onto another screen,
// so the first right-click after using the main screen landed 100 px short (VM-verified).
// The pointer first lands mid-tablet (crossing there costs 100 px but stays on the tablet),
// and only then moves to the target, a move within one screen that the barrier ignores.
TEST(ClickSchedule, LandsOnTheTabletBeforeMovingToTheTarget) {
  ClickSchedule s;
  s.start(0, 5, 6);
  auto a = s.due(0);
  ASSERT_EQ(a.size(), 1u);
  EXPECT_EQ(a[0], ClickSchedule::Step::Land);
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
  s.due(0);   // land
  auto a = s.due(15);
  ASSERT_EQ(a.size(), 2u);
  EXPECT_EQ(a[0], ClickSchedule::Step::Wiggle);
  EXPECT_EQ(a[1], ClickSchedule::Step::Move);
}

// A second right-click 30..110 ms after the first arrived while the button was still down,
// so its Move dragged with the right button held. Cancelling releases a held button.
TEST(ClickSchedule, CancelReleasesAHeldButton) {
  ClickSchedule s;
  s.start(0, 5, 6);
  s.due(50);   // land, wiggle, move, press
  auto c = s.cancel();
  ASSERT_EQ(c.size(), 1u);
  EXPECT_EQ(c[0], ClickSchedule::Step::Release);
  EXPECT_FALSE(s.active());
  EXPECT_TRUE(s.due(1000).empty());
}

TEST(ClickSchedule, CancelBeforePressSendsNothing) {
  ClickSchedule s;
  s.start(0, 5, 6);
  s.due(15);   // land, wiggle, move only
  EXPECT_TRUE(s.cancel().empty());
  EXPECT_FALSE(s.active());
}

TEST(ClickSchedule, CancelWhenIdleOrDoneSendsNothing) {
  ClickSchedule s;
  EXPECT_TRUE(s.cancel().empty());
  s.start(0, 5, 6);
  s.due(1000); // complete
  EXPECT_TRUE(s.cancel().empty());
}

// Fingers of a two-finger tap must leave as a CANCEL, not a release: Qt turns the first touch
// into a mouse press, so a release clicked whatever was under that finger (a link, a button)
// on top of opening the context menu. Everything that is not a tap still releases normally.
TEST(TwoFingerTap, TapFingersAreCancelledNotReleased) {
  TwoFingerTap g;
  EXPECT_FALSE(g.update({{0, 1000, 2000, 0}}, 0).cancelLifts);
  g.update({{0, 1000, 2000, 0}, {1, 3000, 4000, 0}}, 20);   // nothing lifts here
  EXPECT_TRUE(g.update({{1, 3000, 4000, 0}}, 60).cancelLifts);   // finger 0 lifts first
  auto r = g.update({}, 90);
  EXPECT_TRUE(r.rightClick);
  EXPECT_TRUE(r.cancelLifts);
}

TEST(TwoFingerTap, ASingleTapStillReleases) {
  TwoFingerTap g;
  g.update({{0, 1000, 2000, 0}}, 0);
  EXPECT_FALSE(g.update({}, 50).cancelLifts);
}

TEST(TwoFingerTap, AScrollOrPinchStillReleases) {
  TwoFingerTap g;
  g.update({{0, 1000, 2000, 0}, {1, 3000, 4000, 0}}, 0);
  g.update({{0, 1000, 9000, 0}, {1, 3000, 11000, 0}}, 50);   // moved: a scroll
  EXPECT_FALSE(g.update({{1, 3000, 11000, 0}}, 80).cancelLifts);
  EXPECT_FALSE(g.update({}, 100).cancelLifts);
}

TEST(TwoFingerTap, ASlowTwoFingerHoldStillReleases) {
  TwoFingerTap g;
  g.update({{0, 1000, 2000, 0}, {1, 3000, 4000, 0}}, 0);
  EXPECT_FALSE(g.update({{1, 3000, 4000, 0}}, 900).cancelLifts);
}
