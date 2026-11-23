#include "port_alloc.h"
#include <gtest/gtest.h>

using namespace droppix;

TEST(PortAlloc, FirstIsBase)      { EXPECT_EQ(allocate_port(27000, {}), 27000); }
TEST(PortAlloc, SkipsUsed)        { EXPECT_EQ(allocate_port(27000, {27000, 27001}), 27002); }
TEST(PortAlloc, FillsHoles)       { EXPECT_EQ(allocate_port(27000, {27000, 27002}), 27001); }
TEST(PortAlloc, FullReturnsNeg1)  { EXPECT_EQ(allocate_port(27000, {27000, 27001, 27002, 27003}, 4), -1); }
TEST(PortAlloc, RespectsCap)      { EXPECT_EQ(allocate_port(27000, {27000}, 1), -1); }

// A device session must not take the base port while the Server listener owns it, even
// when the listener is between re-arms and the port is momentarily free.
TEST(PortAlloc, ReservedBaseIsSkippedWhenFree) {
  EXPECT_EQ(allocate_port(27000, {}, 4, /*reserve_base=*/true), 27001);
}
TEST(PortAlloc, ReservedBaseStillFillsHoles) {
  EXPECT_EQ(allocate_port(27000, {27001}, 4, /*reserve_base=*/true), 27002);
}
TEST(PortAlloc, ReservedBaseCountsAgainstCap) {
  EXPECT_EQ(allocate_port(27000, {27001, 27002, 27003}, 4, /*reserve_base=*/true), -1);
}
