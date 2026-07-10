#include <gtest/gtest.h>
#include "monitor_geometry.h"
using namespace droppix;

static const char* kSample =
  "Output: 1 HDMI-A-3 333136d1\n"
  "\tenabled\n"
  "\tGeometry: 0,0 1600x900\n"
  "Output: 2 DP-2 fef981ef\n"
  "\tenabled\n"
  "\tGeometry: 1600,0 1920x1080\n"
  "Output: 3 Unknown-1 abcd\n"
  "\tenabled\n"
  "\tGeometry: 3520,0 2560x1600\n";

TEST(MonitorGeometry, ParsesOutputs) {
  auto outs = parse_kscreen_outputs(kSample);
  ASSERT_EQ(outs.size(), 3u);
  EXPECT_EQ(outs[0].name, "HDMI-A-3");
  EXPECT_TRUE(outs[2].enabled);
  EXPECT_EQ(outs[2].geom.x, 3520);
  EXPECT_EQ(outs[2].geom.w, 2560);
  EXPECT_EQ(outs[2].geom.h, 1600);
}
TEST(MonitorGeometry, DesktopBounds) {
  Rect b = desktop_bounds(parse_kscreen_outputs(kSample));
  EXPECT_EQ(b.w, 3520 + 2560);   // 6080
  EXPECT_EQ(b.h, 1600);
}
TEST(MonitorGeometry, SelectsDroppixBySizeMatch) {
  Rect r;
  ASSERT_TRUE(select_droppix(parse_kscreen_outputs(kSample), 2560, 1600, r));
  EXPECT_EQ(r.x, 3520); EXPECT_EQ(r.w, 2560); EXPECT_EQ(r.h, 1600);
}
TEST(MonitorGeometry, SelectFailsWhenNoMatch) {
  Rect r;
  EXPECT_FALSE(select_droppix(parse_kscreen_outputs(kSample), 1234, 5678, r));
}

TEST(MonitorGeometry, SelectsNewOutputByDiffEvenIfSameSize) {
  auto before = parse_kscreen_outputs(
    "Output: 1 DP-2 x\n\tenabled\n\tGeometry: 0,0 1920x1080\n"
    "Output: 2 DP-3 y\n\tenabled\n\tGeometry: 1920,0 1920x1080\n");
  auto after = parse_kscreen_outputs(
    "Output: 1 DP-2 x\n\tenabled\n\tGeometry: 0,0 1920x1080\n"
    "Output: 2 DP-3 y\n\tenabled\n\tGeometry: 1920,0 1920x1080\n"
    "Output: 3 Unknown-1 z\n\tenabled\n\tGeometry: 3840,0 1920x1080\n");
  Rect r;
  ASSERT_TRUE(select_new_output(before, after, r));
  EXPECT_EQ(r.x, 3840); EXPECT_EQ(r.w, 1920); EXPECT_EQ(r.h, 1080);
}
TEST(MonitorGeometry, NoNewOutputWhenUnchanged) {
  auto outs = parse_kscreen_outputs(
    "Output: 1 DP-2 x\n\tenabled\n\tGeometry: 0,0 1920x1080\n");
  Rect r;
  EXPECT_FALSE(select_new_output(outs, outs, r));
}

// Real `xrandr --query` shape: header lines at column 0, indented mode lines,
// a global "Screen N:" line, optional "primary", and disconnected outputs.
static const char* kXrandrSample =
  "Screen 0: minimum 320 x 200, current 3840 x 1080, maximum 16384 x 16384\n"
  "eDP-1 connected primary 1920x1080+0+0 (normal left inverted right x axis y axis) 309mm x 173mm\n"
  "   1920x1080     60.05*+  60.05\n"
  "   1280x720      60.05\n"
  "HDMI-1 disconnected (normal left inverted right x axis y axis)\n"
  "DVI-I-1-1 connected 1920x1080+1920+0 (normal left inverted right x axis y axis) 0mm x 0mm\n"
  "   1920x1080     60.00*\n";

TEST(MonitorGeometry, ParsesXrandrOutputs) {
  auto outs = parse_xrandr_outputs(kXrandrSample);
  ASSERT_EQ(outs.size(), 3u);
  EXPECT_EQ(outs[0].name, "eDP-1");
  EXPECT_TRUE(outs[0].enabled);   // "primary" token must be skipped, not break parsing
  EXPECT_EQ(outs[0].geom.x, 0); EXPECT_EQ(outs[0].geom.w, 1920); EXPECT_EQ(outs[0].geom.h, 1080);
  EXPECT_EQ(outs[1].name, "HDMI-1");
  EXPECT_FALSE(outs[1].enabled);
  EXPECT_EQ(outs[2].name, "DVI-I-1-1");
  EXPECT_TRUE(outs[2].enabled);
  EXPECT_EQ(outs[2].geom.x, 1920);
}
TEST(MonitorGeometry, XrandrConnectedButInactiveIsDisabled) {
  // Connected with no active mode: no WxH+X+Y before the "(...)" flags.
  auto outs = parse_xrandr_outputs(
    "eDP-1 connected (normal left inverted right x axis y axis)\n");
  ASSERT_EQ(outs.size(), 1u);
  EXPECT_FALSE(outs[0].enabled);
}
TEST(MonitorGeometry, XrandrFeedsSelectNewOutput) {
  // The backend-agnostic diff helper works on xrandr-parsed outputs unchanged.
  auto before = parse_xrandr_outputs(
    "eDP-1 connected primary 1920x1080+0+0 (normal) 309mm x 173mm\n");
  auto after = parse_xrandr_outputs(
    "eDP-1 connected primary 1920x1080+0+0 (normal) 309mm x 173mm\n"
    "DVI-I-1-1 connected 1920x1080+1920+0 (normal) 0mm x 0mm\n");
  Rect r;
  ASSERT_TRUE(select_new_output(before, after, r));
  EXPECT_EQ(r.x, 1920); EXPECT_EQ(r.w, 1920); EXPECT_EQ(r.h, 1080);
}

TEST(MonitorGeometry, SameSizePreexistingScreenNeverWinsSizeMatch) {
  // Laptop panel (eDP-1) and the tablet share 1920x1080. eDP-1 was enabled BEFORE the
  // source created its monitor, so it must not be picked; the new DVI-I-1-1 must win.
  auto before = parse_xrandr_outputs(
    "eDP-1 connected primary 1920x1080+0+0 (normal) 309mm x 173mm\n");
  auto after = parse_xrandr_outputs(
    "eDP-1 connected primary 1920x1080+0+0 (normal) 309mm x 173mm\n"
    "DVI-I-1-1 connected 1920x1080+1920+0 (normal) 480mm x 270mm\n");
  OutputInfo o;
  ASSERT_TRUE(select_droppix(after, before, 1920, 1080, o));
  EXPECT_EQ(o.name, "DVI-I-1-1");
}
TEST(MonitorGeometry, X11EvdiConnectorNamePreferredEvenAcrossRestart) {
  // Session restart: the droppix output already existed in `before` (teardown lag).
  // The DVI-I-1-<n> secondary-GPU signature still identifies it.
  auto outs = parse_xrandr_outputs(
    "eDP-1 connected primary 1920x1080+0+0 (normal) 309mm x 173mm\n"
    "DVI-I-1-1 connected 1920x1080+1920+0 (normal) 480mm x 270mm\n");
  OutputInfo o;
  ASSERT_TRUE(select_droppix(outs, outs, 1920, 1080, o));
  EXPECT_EQ(o.name, "DVI-I-1-1");
}
TEST(MonitorGeometry, RealDviPortWithoutProviderSuffixNotPreferred) {
  // "DVI-I-1" (no trailing -<n>) is a physical port; enabled-in-before excludes it.
  auto outs = parse_xrandr_outputs(
    "DVI-I-1 connected primary 1920x1080+0+0 (normal) 509mm x 286mm\n");
  OutputInfo o;
  EXPECT_FALSE(select_droppix(outs, outs, 1920, 1080, o));
}

TEST(MonitorGeometry, StripsAnsiColorCodes) {
  // kscreen-doctor colorizes output even through a pipe; the parser must cope.
  std::string colored =
    "\033[01;32mOutput: \033[0;0m1 HDMI-A-3 uid1\n"
    "\t\033[01;32menabled\033[0;0m\n"
    "\t\033[01;33mGeometry: \033[0;0m0,0 1600x900\n"
    "\033[01;32mOutput: \033[0;0m2 DVI-I-1 uid2\n"
    "\t\033[01;32menabled\033[0;0m\n"
    "\t\033[01;33mGeometry: \033[0;0m1920,900 800x600\n";
  auto outs = parse_kscreen_outputs(colored);
  ASSERT_EQ(outs.size(), 2u);
  EXPECT_EQ(outs[0].name, "HDMI-A-3");
  EXPECT_TRUE(outs[0].enabled);
  EXPECT_EQ(outs[1].name, "DVI-I-1");
  EXPECT_EQ(outs[1].geom.x, 1920); EXPECT_EQ(outs[1].geom.w, 800); EXPECT_EQ(outs[1].geom.h, 600);
}
