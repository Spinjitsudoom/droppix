#include "approved_store.h"
#include <QTemporaryDir>
#include <gtest/gtest.h>

TEST(ApprovedStore, PersistsAcrossInstances) {
  QTemporaryDir d;
  { droppix::ApprovedStore s(d.path()); EXPECT_FALSE(s.isApproved("a")); s.approve("a"); }
  droppix::ApprovedStore s2(d.path());
  EXPECT_TRUE(s2.isApproved("a")); EXPECT_FALSE(s2.isApproved("b"));
  s2.clear(); EXPECT_FALSE(s2.isApproved("a"));
}
