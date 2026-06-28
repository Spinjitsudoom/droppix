#include <gtest/gtest.h>

#include "wake.h"

using namespace droppix;

TEST(Wake, RoundTrip) {
  auto b = encode_wake(27000);
  uint16_t p = 0;
  ASSERT_EQ(b.size(), 6u);
  EXPECT_EQ(b[0], 'D');
  EXPECT_EQ(b[1], 'P');
  EXPECT_EQ(b[2], 'X');
  EXPECT_EQ(b[3], 'W');
  EXPECT_EQ(b[4], 27000 >> 8);
  EXPECT_EQ(b[5], 27000 & 0xFF);
  ASSERT_TRUE(decode_wake(b, p));
  EXPECT_EQ(p, 27000);
}

TEST(Wake, RejectsBadMagicOrLen) {
  uint16_t p;
  EXPECT_FALSE(decode_wake({'X', 'X', 'X', 'X', 0, 0}, p));
  EXPECT_FALSE(decode_wake({'D', 'P', 'X', 'W', 0}, p));  // too short
}
