#include <gtest/gtest.h>
#include <algorithm>
#include "args_builder.h"

using namespace droppix;

static bool has(const std::vector<std::string>& v, const std::string& a) {
  for (auto& s : v) if (s == a) return true;
  return false;
}
static std::string valafter(const std::vector<std::string>& v, const std::string& flag) {
  for (size_t i = 0; i + 1 < v.size(); ++i) if (v[i] == flag) return v[i + 1];
  return "";
}

TEST(ArgsBuilder, PerSessionPortAndTouchName) {
  Settings s; s.source = Settings::Source::Evdi; s.touch = true; s.port = 27000;
  Command c = build_command(s, "/x", 27003, "droppix-touch-27003");
  EXPECT_EQ(valafter(c.args, "--port"), "27003");
  EXPECT_TRUE(has(c.args, "--touch"));
  EXPECT_EQ(valafter(c.args, "--touch-name"), "droppix-touch-27003");
}
TEST(ArgsBuilder, DefaultPortIsSettingsPort) {
  Settings s; s.source = Settings::Source::Evdi; s.port = 27050;
  Command c = build_command(s, "/x");   // port = -1 default
  EXPECT_EQ(valafter(c.args, "--port"), "27050");
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
}

TEST(ArgsBuilder, EvdiUsesPkexecWithModeFlags) {
  Settings s; s.source = Settings::Source::Evdi;
  s.width = 2560; s.height = 1600; s.refresh_hz = 60;
  Command c = build_command(s, "/path/droppix_stream");
  EXPECT_EQ(c.program, "pkexec");
  EXPECT_EQ(c.args.front(), "/path/droppix_stream");
  EXPECT_FALSE(has(c.args, "--test-pattern"));
  EXPECT_TRUE(has(c.args, "--width"));  EXPECT_TRUE(has(c.args, "2560"));
  EXPECT_TRUE(has(c.args, "--height")); EXPECT_TRUE(has(c.args, "1600"));
  EXPECT_TRUE(has(c.args, "--refresh")); EXPECT_TRUE(has(c.args, "60"));
  EXPECT_TRUE(has(c.args, "--stats-json"));
  EXPECT_FALSE(has(c.args, "--touch"));   // off by default
}

TEST(ArgsBuilder, EvdiTouchFlagWhenEnabled) {
  Settings s; s.source = Settings::Source::Evdi; s.touch = true;
  Command c = build_command(s, "/path/droppix_stream");
  EXPECT_TRUE(has(c.args, "--touch"));
}
TEST(ArgsBuilder, TestPatternNeverHasTouch) {
  Settings s; s.source = Settings::Source::TestPattern; s.touch = true;
  Command c = build_command(s, "/path/droppix_stream");
  EXPECT_FALSE(has(c.args, "--touch"));   // touch is evdi-only
}

TEST(ArgsBuilder, EvdiOrientationFlagWhenSet) {
  Settings s; s.source = Settings::Source::Evdi; s.orientation = 90;
  Command c = build_command(s, "/path/droppix_stream");
  EXPECT_TRUE(has(c.args, "--orientation")); EXPECT_TRUE(has(c.args, "90"));
}
TEST(ArgsBuilder, EvdiNoOrientationFlagWhenZero) {
  Settings s; s.source = Settings::Source::Evdi; s.orientation = 0;
  Command c = build_command(s, "/path/droppix_stream");
  EXPECT_FALSE(has(c.args, "--orientation"));
}
TEST(ArgsBuilder, TestPatternNeverHasOrientation) {
  Settings s; s.source = Settings::Source::TestPattern; s.orientation = 180;
  Command c = build_command(s, "/path/droppix_stream");
  EXPECT_FALSE(has(c.args, "--orientation"));   // evdi-only
}

TEST(ArgsBuilder, EvdiHasApproveFlag) {
  Settings s; s.source = Settings::Source::Evdi;
  Command c = build_command(s, "/path/droppix_stream");
  EXPECT_TRUE(has(c.args, "--approve"));
}
TEST(ArgsBuilder, TestPatternNeverHasApprove) {
  Settings s; s.source = Settings::Source::TestPattern;
  Command c = build_command(s, "/path/droppix_stream");
  EXPECT_FALSE(has(c.args, "--approve"));   // approval gate is evdi-only
}

TEST(ArgsBuilder, TlsFlagsPresentWhenCertSet) {
  Settings s; s.source = Settings::Source::TestPattern;
  s.tls = true; s.certPath = "/cfg/cert.pem"; s.keyPath = "/cfg/key.pem";
  Command c = build_command(s, "/path/droppix_stream");
  EXPECT_TRUE(has(c.args, "--tls"));
  EXPECT_TRUE(has(c.args, "--cert")); EXPECT_TRUE(has(c.args, "/cfg/cert.pem"));
  EXPECT_TRUE(has(c.args, "--key"));  EXPECT_TRUE(has(c.args, "/cfg/key.pem"));
}

TEST(ArgsBuilder, TlsFlagsPresentForEvdiTooWhenCertSet) {
  Settings s; s.source = Settings::Source::Evdi;
  s.tls = true; s.certPath = "/cfg/cert.pem"; s.keyPath = "/cfg/key.pem";
  Command c = build_command(s, "/path/droppix_stream");
  EXPECT_TRUE(has(c.args, "--tls"));
  EXPECT_TRUE(has(c.args, "--cert")); EXPECT_TRUE(has(c.args, "/cfg/cert.pem"));
}

TEST(ArgsBuilder, NoTlsFlagsWhenCertPathEmpty) {
  Settings s; s.source = Settings::Source::TestPattern;
  s.tls = true; s.certPath.clear(); s.keyPath.clear();
  Command c = build_command(s, "/path/droppix_stream");
  EXPECT_FALSE(has(c.args, "--tls"));
}

TEST(ArgsBuilder, NoTlsFlagsWhenTlsDisabled) {
  Settings s; s.source = Settings::Source::TestPattern;
  s.tls = false; s.certPath = "/cfg/cert.pem"; s.keyPath = "/cfg/key.pem";
  Command c = build_command(s, "/path/droppix_stream");
  EXPECT_FALSE(has(c.args, "--tls"));
}

TEST(ArgsBuilder, AudioFlagAppendedWhenEnabled) {
  droppix::Settings s; s.source = droppix::Settings::Source::Evdi; s.audio = true;
  auto c = droppix::build_command(s, "/usr/bin/droppix_stream");
  bool has_audio = false;
  for (auto& a : c.args) if (a == "--audio") has_audio = true;
  EXPECT_TRUE(has_audio);

  droppix::Settings s2; s2.source = droppix::Settings::Source::Evdi; s2.audio = false;
  auto c2 = droppix::build_command(s2, "/usr/bin/droppix_stream");
  for (auto& a : c2.args) EXPECT_NE(a, std::string("--audio"));
}

TEST(ArgsBuilder, UsbAoaAddsSerialAndOmitsTls) {
  droppix::Settings s;
  s.source = droppix::Settings::Source::Evdi;
  s.tls = true;
  s.certPath = "/tmp/cert.pem";
  s.keyPath = "/tmp/key.pem";
  auto c = droppix::build_command(s, "/opt/droppix_stream", 27001, "droppix-touch-27001",
                                  "R32D204ZH6J");
  // evdi -> pkexec wrapper; args include the binary first.
  EXPECT_EQ(c.program, "pkexec");
  auto has = [&](const std::string& flag) {
    return std::find(c.args.begin(), c.args.end(), flag) != c.args.end();
  };
  // --usb-aoa <serial> present, in order.
  auto it = std::find(c.args.begin(), c.args.end(), "--usb-aoa");
  ASSERT_NE(it, c.args.end());
  ASSERT_NE(std::next(it), c.args.end());
  EXPECT_EQ(*std::next(it), "R32D204ZH6J");
  // TLS omitted for the cable.
  EXPECT_FALSE(has("--tls"));
  EXPECT_FALSE(has("--cert"));
  // evdi flags still present.
  EXPECT_TRUE(has("--refresh"));
  EXPECT_TRUE(has("--width"));
}

TEST(ArgsBuilder, NoUsbAoaByDefault) {
  droppix::Settings s;
  s.source = droppix::Settings::Source::Evdi;
  s.tls = true;
  s.certPath = "/tmp/cert.pem";
  s.keyPath = "/tmp/key.pem";
  auto c = droppix::build_command(s, "/opt/droppix_stream", 27000, "droppix-touch");
  auto has = [&](const std::string& flag) {
    return std::find(c.args.begin(), c.args.end(), flag) != c.args.end();
  };
  EXPECT_FALSE(has("--usb-aoa"));
  EXPECT_TRUE(has("--tls"));   // unchanged behavior when no serial
}
