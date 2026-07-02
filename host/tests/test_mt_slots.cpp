#include "mt_slots.h"
#include <gtest/gtest.h>

using namespace droppix;

TEST(MtSlots, AssignsLowestFreeSlotsToNewContacts) {
  MtSlots m;
  auto u = m.update({{5, 10, 20, 100}, {7, 30, 40, 200}});
  ASSERT_EQ(u.active.size(), 2u);
  EXPECT_TRUE(u.lifted.empty());
  EXPECT_EQ(u.active[0].slot, 0); EXPECT_TRUE(u.active[0].isNew); EXPECT_EQ(u.active[0].c.id, 5);
  EXPECT_EQ(u.active[1].slot, 1); EXPECT_TRUE(u.active[1].isNew); EXPECT_EQ(u.active[1].c.id, 7);
}

TEST(MtSlots, HoldsSlotsAcrossMoves) {
  MtSlots m;
  m.update({{5, 0, 0, 0}, {7, 0, 0, 0}});
  auto u = m.update({{5, 1, 1, 1}, {7, 2, 2, 2}});
  ASSERT_EQ(u.active.size(), 2u); EXPECT_TRUE(u.lifted.empty());
  EXPECT_EQ(u.active[0].slot, 0); EXPECT_FALSE(u.active[0].isNew);
  EXPECT_EQ(u.active[1].slot, 1); EXPECT_FALSE(u.active[1].isNew);
}

TEST(MtSlots, LiftsVanishedContactAndReusesItsSlot) {
  MtSlots m;
  m.update({{5, 0, 0, 0}, {7, 0, 0, 0}});   // ids 5,7 -> slots 0,1
  auto u = m.update({{7, 1, 1, 1}});         // id 5 lifted
  ASSERT_EQ(u.lifted.size(), 1u); EXPECT_EQ(u.lifted[0], 0);
  ASSERT_EQ(u.active.size(), 1u); EXPECT_EQ(u.active[0].slot, 1); EXPECT_FALSE(u.active[0].isNew);
  auto u2 = m.update({{7, 2, 2, 2}, {9, 3, 3, 3}});   // new id 9 reuses freed slot 0
  bool ok = false;
  for (const auto& a : u2.active) if (a.c.id == 9) { EXPECT_EQ(a.slot, 0); EXPECT_TRUE(a.isNew); ok = true; }
  EXPECT_TRUE(ok);
}

TEST(MtSlots, EmptySetLiftsEverything) {
  MtSlots m;
  m.update({{5, 0, 0, 0}, {7, 0, 0, 0}});
  auto u = m.update({});
  EXPECT_TRUE(u.active.empty());
  EXPECT_EQ(u.lifted.size(), 2u);
}

TEST(MtSlots, DropsContactsBeyondSlotBudget) {
  MtSlots m(2);
  auto u = m.update({{1, 0, 0, 0}, {2, 0, 0, 0}, {3, 0, 0, 0}});
  EXPECT_EQ(u.active.size(), 2u);   // third contact dropped
}
