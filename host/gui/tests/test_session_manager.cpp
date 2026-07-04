#include "session_manager.h"
#include <gtest/gtest.h>

using namespace droppix;

static Session mk(const QString& key, int port) {
  Session s; s.key = key; s.port = port; s.controller = nullptr; return s;
}

TEST(SessionManager, AddFindHasCount) {
  SessionManager m;
  EXPECT_FALSE(m.has("a")); EXPECT_EQ(m.count(), 0);
  m.add(mk("a", 27000));
  m.add(mk("b", 27001));
  EXPECT_TRUE(m.has("a")); EXPECT_TRUE(m.has("b")); EXPECT_FALSE(m.has("c"));
  EXPECT_EQ(m.count(), 2);
  ASSERT_NE(m.find("b"), nullptr); EXPECT_EQ(m.find("b")->port, 27001);
  EXPECT_EQ(m.find("c"), nullptr);
}

TEST(SessionManager, UsedPortsAndAllocateSkip) {
  SessionManager m;
  m.add(mk("a", 27000)); m.add(mk("b", 27001));
  EXPECT_EQ(m.usedPorts(), (std::set<int>{27000, 27001}));
  EXPECT_EQ(m.allocatePort(27000), 27002);
}

TEST(SessionManager, RemoveDropsOneAndFreesPort) {
  SessionManager m;
  m.add(mk("a", 27000)); m.add(mk("b", 27001));
  m.remove("a");
  EXPECT_FALSE(m.has("a")); EXPECT_TRUE(m.has("b")); EXPECT_EQ(m.count(), 1);
  EXPECT_EQ(m.allocatePort(27000), 27000);   // 27000 freed
}

TEST(SessionManager, KeysListsActiveKeys) {
  SessionManager m;
  EXPECT_TRUE(m.keys().isEmpty());
  m.add(mk("a", 27000)); m.add(mk("b", 27001));
  EXPECT_EQ(m.keys(), (QSet<QString>{QString("a"), QString("b")}));
  m.remove("a");
  EXPECT_EQ(m.keys(), (QSet<QString>{QString("b")}));
}
