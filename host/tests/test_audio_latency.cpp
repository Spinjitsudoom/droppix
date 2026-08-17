#include <gtest/gtest.h>

#include "audio_latency.h"

using namespace droppix;

TEST(AudioLatency, BudgetIsTheAdvertisedDuration) {
  // 200 ms of s16le/48k/stereo = 0.2 * 192000.
  EXPECT_EQ(audio_max_queue_bytes(), 38400u);
}

TEST(AudioLatency, NothingDroppedInsideTheBudget) {
  EXPECT_EQ(audio_overflow_drop_bytes(0), 0u);
  EXPECT_EQ(audio_overflow_drop_bytes(audio_max_queue_bytes()), 0u);
  EXPECT_EQ(audio_overflow_drop_bytes(audio_max_queue_bytes() - 1), 0u);
}

// The point of the low-water mark. Trimming to exactly the limit leaves the queue pinned at
// the ceiling, so every later chunk keeps arriving late for the rest of the session — the
// bug this whole change is about. Dropping to half the budget buys real headroom.
TEST(AudioLatency, OverflowDropsToLowWaterNotToTheLimit) {
  const size_t limit = audio_max_queue_bytes();
  const size_t queued = limit * 4;
  const size_t drop = audio_overflow_drop_bytes(queued);

  ASSERT_GT(drop, 0u);
  const size_t remaining = queued - drop;
  EXPECT_EQ(remaining, limit / 2);
  EXPECT_LT(remaining, limit) << "must leave headroom, not sit exactly at the ceiling";
}

TEST(AudioLatency, JustOverTheLimitStillDropsToLowWater) {
  const size_t limit = audio_max_queue_bytes();
  const size_t drop = audio_overflow_drop_bytes(limit + 1);
  EXPECT_EQ(limit + 1 - drop, limit / 2);
}

// A backlog measured in seconds must come back to a fraction of a second in one step, not
// bleed down gradually — gradual recovery is indistinguishable from staying late.
TEST(AudioLatency, RecoversFromASecondsLongBacklogImmediately) {
  const size_t five_seconds = kAudioBytesPerSec * 5;
  const size_t remaining = five_seconds - audio_overflow_drop_bytes(five_seconds);
  const double remaining_ms = 1000.0 * remaining / kAudioBytesPerSec;
  EXPECT_LE(remaining_ms, kAudioMaxQueueMs / 2.0 + 1.0);
}
