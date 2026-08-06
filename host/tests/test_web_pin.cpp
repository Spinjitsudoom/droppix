#include <gtest/gtest.h>
#include "web_pin.h"

using droppix::pin_matches;

TEST(WebPin, MatchesExact) {
  EXPECT_TRUE(pin_matches("042913", "042913"));
}
TEST(WebPin, RejectsWrong) {
  EXPECT_FALSE(pin_matches("042914", "042913"));
  EXPECT_FALSE(pin_matches("142913", "042913"));
}
TEST(WebPin, RejectsLengthMismatch) {
  EXPECT_FALSE(pin_matches("0429", "042913"));    // too short
  EXPECT_FALSE(pin_matches("0429130", "042913")); // too long
  EXPECT_FALSE(pin_matches("", "042913"));        // empty
}
TEST(WebPin, MessageTypeConstants) {
  // Web-only pairing types must not collide with the shared MsgType range (1..15).
  EXPECT_GT(droppix::kMsgPair, 15);
  EXPECT_NE(droppix::kMsgPair, droppix::kMsgPairResult);
  EXPECT_EQ(droppix::kWebPinMaxTries, 5);
}
