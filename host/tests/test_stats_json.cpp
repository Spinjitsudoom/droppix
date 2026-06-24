#include <gtest/gtest.h>
#include "stats_json.h"

using droppix::format_stats_json;

TEST(StatsJson, ContainsAllFieldsAndValues) {
  std::string j = format_stats_json(4.2, 7.1, 30.0, 36.0, 74.5, true);
  EXPECT_NE(j.find("\"encode_ms_avg\":4.2"), std::string::npos);
  EXPECT_NE(j.find("\"encode_ms_peak\":7.1"), std::string::npos);
  EXPECT_NE(j.find("\"fps\":30.0"), std::string::npos);
  EXPECT_NE(j.find("\"frame_kb_avg\":36.0"), std::string::npos);
  EXPECT_NE(j.find("\"frame_kb_peak\":74.5"), std::string::npos);
  EXPECT_NE(j.find("\"client_connected\":true"), std::string::npos);
  EXPECT_EQ(j.front(), '{');
  EXPECT_EQ(j.back(), '}');
}

TEST(StatsJson, BoolFalseRendersFalse) {
  std::string j = format_stats_json(0.0, 0.0, 0.0, 0.0, 0.0, false);
  EXPECT_NE(j.find("\"client_connected\":false"), std::string::npos);
}
