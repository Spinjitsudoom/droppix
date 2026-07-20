#include "server_control.h"
#include <gtest/gtest.h>

using namespace droppix;

TEST(ServerControl, DisabledNeverRearms) {
  EXPECT_FALSE(shouldRearm(false, 100000, true));
  EXPECT_FALSE(shouldRearm(false, 0, false));
}

TEST(ServerControl, RearmsAfterRealSession) {
  EXPECT_TRUE(shouldRearm(true, 60000, true));               // ran long, had a client
  EXPECT_TRUE(shouldRearm(true, 500, true));                 // short but had a client
  EXPECT_TRUE(shouldRearm(true, kServerMinRunMs, false));    // ran >= threshold, still waiting
}

TEST(ServerControl, FailedStartDoesNotRearm) {
  EXPECT_FALSE(shouldRearm(true, 100, false));               // died fast, never connected
  EXPECT_FALSE(shouldRearm(true, kServerMinRunMs - 1, false));
}
