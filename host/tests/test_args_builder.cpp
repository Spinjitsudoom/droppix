#include <gtest/gtest.h>
#include "args_builder.h"

using namespace droppix;

static bool has(const std::vector<std::string>& v, const std::string& a) {
  for (auto& s : v) if (s == a) return true;
  return false;
}

TEST(ArgsBuilder, TestPatternRunsBinaryDirectly) {
  Settings s; s.source = Settings::Source::TestPattern;
  s.width = 1280; s.height = 720; s.fps = 30; s.bitrate_kbps = 8000; s.port = 27000;
  Command c = build_command(s, "/path/droppix_stream");
  EXPECT_EQ(c.program, "/path/droppix_stream");
  EXPECT_TRUE(has(c.args, "--test-pattern"));
  EXPECT_TRUE(has(c.args, "--width")); EXPECT_TRUE(has(c.args, "1280"));
  EXPECT_TRUE(has(c.args, "--height")); EXPECT_TRUE(has(c.args, "720"));
  EXPECT_TRUE(has(c.args, "--stats-json"));
  EXPECT_TRUE(c.needs_adb_reverse);  // auto_adb_reverse default true
}

TEST(ArgsBuilder, EvdiUsesPkexecAndNoTestPattern) {
  Settings s; s.source = Settings::Source::Evdi;
  Command c = build_command(s, "/path/droppix_stream");
  EXPECT_EQ(c.program, "pkexec");
  EXPECT_EQ(c.args.front(), "/path/droppix_stream");  // pkexec's first arg is the binary
  EXPECT_FALSE(has(c.args, "--test-pattern"));
  EXPECT_FALSE(has(c.args, "--width"));               // evdi resolution is engine-fixed
  EXPECT_TRUE(has(c.args, "--stats-json"));
}

TEST(ArgsBuilder, AutoAdbReverseToggle) {
  Settings s; s.auto_adb_reverse = false;
  EXPECT_FALSE(build_command(s, "/x").needs_adb_reverse);
}
