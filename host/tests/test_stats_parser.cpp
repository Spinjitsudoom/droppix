#include <gtest/gtest.h>
#include "stats_parser.h"
#include "stats_json.h"   // cross-check against the engine's formatter

using namespace droppix;

TEST(StatsParser, ParsesEngineOutput) {
  std::string line = format_stats_json(4.2, 7.1, 30.0, 36.0, 74.5, true);
  Stats s = parse_stats_json(line);
  ASSERT_TRUE(s.valid);
  EXPECT_DOUBLE_EQ(s.encode_ms_avg, 4.2);
  EXPECT_DOUBLE_EQ(s.encode_ms_peak, 7.1);
  EXPECT_DOUBLE_EQ(s.fps, 30.0);
  EXPECT_DOUBLE_EQ(s.frame_kb_avg, 36.0);
  EXPECT_DOUBLE_EQ(s.frame_kb_peak, 74.5);
  EXPECT_TRUE(s.client_connected);
}

TEST(StatsParser, FalseConnected) {
  Stats s = parse_stats_json(format_stats_json(1, 2, 3, 4, 5, false));
  ASSERT_TRUE(s.valid);
  EXPECT_FALSE(s.client_connected);
}

TEST(StatsParser, GarbageIsInvalid) {
  EXPECT_FALSE(parse_stats_json("stats: encode avg 4 ms ...").valid);
  EXPECT_FALSE(parse_stats_json("").valid);
  EXPECT_FALSE(parse_stats_json("{}").valid);
}
