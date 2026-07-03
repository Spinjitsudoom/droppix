#include "port_alloc.h"
#include <gtest/gtest.h>

using namespace droppix;

TEST(PortAlloc, FirstIsBase)      { EXPECT_EQ(allocate_port(27000, {}), 27000); }
TEST(PortAlloc, SkipsUsed)        { EXPECT_EQ(allocate_port(27000, {27000, 27001}), 27002); }
TEST(PortAlloc, FillsHoles)       { EXPECT_EQ(allocate_port(27000, {27000, 27002}), 27001); }
TEST(PortAlloc, FullReturnsNeg1)  { EXPECT_EQ(allocate_port(27000, {27000, 27001, 27002, 27003}, 4), -1); }
TEST(PortAlloc, RespectsCap)      { EXPECT_EQ(allocate_port(27000, {27000}, 1), -1); }
