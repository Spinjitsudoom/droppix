#include <gtest/gtest.h>
#include "stats_json.h"

using droppix::format_stats_json;

TEST(StatsJson, ContainsAllFieldsAndValues) {
  std::string j = format_stats_json(4.2, 7.1, 30.0, 36.0, 74.5, true, 33.3, 1.5, 9.9);
  EXPECT_NE(j.find("\"encode_ms_avg\":4.2"), std::string::npos);
  EXPECT_NE(j.find("\"encode_ms_peak\":7.1"), std::string::npos);
  EXPECT_NE(j.find("\"fps\":30.0"), std::string::npos);
  EXPECT_NE(j.find("\"frame_kb_avg\":36.0"), std::string::npos);
  EXPECT_NE(j.find("\"frame_kb_peak\":74.5"), std::string::npos);
  EXPECT_NE(j.find("\"client_connected\":true"), std::string::npos);
  EXPECT_NE(j.find("\"interval_ms_avg\":33.3"), std::string::npos);
  EXPECT_NE(j.find("\"send_ms_avg\":1.5"), std::string::npos);
  EXPECT_NE(j.find("\"send_ms_peak\":9.9"), std::string::npos);
  EXPECT_EQ(j.front(), '{');
  EXPECT_EQ(j.back(), '}');
}

TEST(StatsJson, InstrumentationFieldsDefaultToZero) {
  // Old six-arg callers still compile and emit zeroed diagnostics — and the GUI parser
  // (key lookup) ignores the extra keys, so this stays backward-compatible.
  std::string j = format_stats_json(1.0, 2.0, 3.0, 4.0, 5.0, true);
  EXPECT_NE(j.find("\"interval_ms_avg\":0.0"), std::string::npos);
  EXPECT_NE(j.find("\"send_ms_avg\":0.0"), std::string::npos);
}

TEST(StatsJson, BoolFalseRendersFalse) {
  std::string j = format_stats_json(0.0, 0.0, 0.0, 0.0, 0.0, false);
  EXPECT_NE(j.find("\"client_connected\":false"), std::string::npos);
}
