#include <gtest/gtest.h>

#include "send_backlog.h"

using droppix::backlog_limit_bytes;
using droppix::should_skip_frame;

TEST(SendBacklog, LimitScalesWithBitrate) {
  // ~250 ms of stream at the session bitrate.
  EXPECT_EQ(backlog_limit_bytes(16000), 16000u * 1000 / 8 / 4);  // 500 KB
  EXPECT_EQ(backlog_limit_bytes(8000), 8000u * 1000 / 8 / 4);    // 250 KB
}

TEST(SendBacklog, LimitHasFloorForTinyBitrates) {
  // Below the floor a single frame could exceed the limit and we would drop constantly.
  EXPECT_EQ(backlog_limit_bytes(500), 64u * 1024u);
  EXPECT_EQ(backlog_limit_bytes(0), 64u * 1024u);
  EXPECT_EQ(backlog_limit_bytes(-1), 64u * 1024u);
}

TEST(SendBacklog, KeepsStreamingWhileLinkDrains) {
  const size_t limit = backlog_limit_bytes(8000);
  EXPECT_FALSE(should_skip_frame(0, limit, false));
  EXPECT_FALSE(should_skip_frame(limit, limit, false));  // at the limit, not over it
}

TEST(SendBacklog, StartsDroppingOnceBacklogExceedsLimit) {
  const size_t limit = backlog_limit_bytes(8000);
  EXPECT_TRUE(should_skip_frame(limit + 1, limit, false));
}

TEST(SendBacklog, HysteresisPreventsOscillation) {
  const size_t limit = backlog_limit_bytes(8000);
  // Once dropping, keep dropping until the backlog halves...
  EXPECT_TRUE(should_skip_frame(limit, limit, true));
  EXPECT_TRUE(should_skip_frame(limit / 2 + 1, limit, true));
  // ...then resume.
  EXPECT_FALSE(should_skip_frame(limit / 2, limit, true));
  EXPECT_FALSE(should_skip_frame(0, limit, true));
}

TEST(SendBacklog, UnknownBacklogNeverDrops) {
  // pending_bytes() reports 0 when unsupported (e.g. AOA/USB); that must read as
  // "healthy link" so those transports keep their existing behaviour.
  EXPECT_FALSE(should_skip_frame(0, backlog_limit_bytes(8000), false));
}
