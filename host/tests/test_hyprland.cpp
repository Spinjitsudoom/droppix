#include "hypr_state.h"
#include "desktop_backend.h"
#include <gtest/gtest.h>

using namespace droppix;

// `hyprctl -j monitors all` (trimmed to the fields droppix reads).
static const char* kMonitors =
  R"([{"id":0,"name":"eDP-1","make":"BOE","model":"0x0A2B","serial":"","width":2560,"height":1600,)"
  R"("refreshRate":60.0,"x":0,"y":0,"scale":1.6,"transform":0,"focused":true,"disabled":false},)"
  R"({"id":1,"name":"DVI-I-1","make":"DpiX, Inc.","model":"droppix","serial":"0x00006978","width":1280,)"
  R"("height":800,"refreshRate":59.91,"x":1600,"y":0,"scale":1.0,"transform":1,"focused":false,"disabled":false},)"
  R"({"id":2,"name":"HDMI-A-1","make":"Dell","model":"U2720Q","serial":"ABC","width":3840,"height":2160,)"
  R"("refreshRate":60.0,"x":0,"y":0,"scale":1.0,"transform":0,"focused":false,"disabled":true}])";

TEST(HyprState, OutputsUseScaledAndRotatedLayoutSize) {
  std::vector<HyprMonitor> mons;
  ASSERT_TRUE(parse_hypr_monitors(kMonitors, mons));
  ASSERT_EQ(mons.size(), 3u);
  EXPECT_EQ(mons[0].info.name, "eDP-1");
  EXPECT_EQ(mons[0].info.geom.w, 1600);   // 2560 / 1.6
  EXPECT_EQ(mons[0].info.geom.h, 1000);
  EXPECT_EQ(mons[1].info.geom.x, 1600);
  EXPECT_EQ(mons[1].info.geom.w, 800);    // rotated 90°
  EXPECT_EQ(mons[1].info.geom.h, 1280);
  EXPECT_TRUE(mons[1].info.enabled);
  EXPECT_FALSE(mons[2].info.enabled);
  EXPECT_EQ(mons[1].mode_w, 1280);
  EXPECT_DOUBLE_EQ(mons[0].scale, 1.6);
}
TEST(HyprState, FindsTheSessionMonitorBySerial) {
  std::vector<HyprMonitor> mons;
  ASSERT_TRUE(parse_hypr_monitors(kMonitors, mons));
  EXPECT_EQ(hypr_find_by_serial(mons, 27000), "DVI-I-1");
  EXPECT_EQ(hypr_find_by_serial(mons, 1), "");
}
TEST(HyprState, RejectsAnErrorReply) {
  std::vector<HyprMonitor> mons;
  EXPECT_FALSE(parse_hypr_monitors("HYPRLAND_INSTANCE_SIGNATURE not set!", mons));
}
TEST(HyprState, MonitorRuleKeepsModeAndScale) {
  std::vector<HyprMonitor> mons;
  ASSERT_TRUE(parse_hypr_monitors(kMonitors, mons));
  EXPECT_EQ(hypr_place_rule(mons, "DVI-I-1", 1600, 0), "DVI-I-1,1280x800@59.91,1600x0,1,transform,1");
  EXPECT_EQ(hypr_place_rule(mons, "HDMI-9", 0, 0), "");
}
TEST(HyprState, MirrorRuleNamesThePrimary) {
  std::vector<HyprMonitor> mons;
  ASSERT_TRUE(parse_hypr_monitors(kMonitors, mons));
  EXPECT_EQ(hypr_mirror_rule(mons, "DVI-I-1", "eDP-1"), "DVI-I-1,1280x800@59.91,auto,1,mirror,eDP-1");
  EXPECT_EQ(hypr_mirror_rule(mons, "DVI-I-1", "x;y"), "");
}
TEST(HyprState, PicksTheLiveInstance) {
  // $XDG_RUNTIME_DIR/hypr/<signature>/hyprland.lock holds the compositor's pid.
  const std::vector<std::string> sigs = {"0aa_1700000000_111", "0bb_1700000500_222"};
  auto pid_of = [](const std::string& s) -> long { return s.find("_111") != std::string::npos ? 111 : 222; };
  EXPECT_EQ(pick_hypr_instance(sigs, pid_of, [](long p) { return p == 222; }), "0bb_1700000500_222");
  EXPECT_EQ(pick_hypr_instance(sigs, pid_of, [](long) { return false; }), "");
  EXPECT_EQ(pick_hypr_instance({"../etc"}, pid_of, [](long) { return true; }), "");   // unsafe name
}

TEST(DesktopBackend, HyprlandSelectsHyprland) {
  EXPECT_EQ(select_backend_kind("Hyprland", false, false, false), BackendKind::Hyprland);
  EXPECT_EQ(select_backend_kind("", false, false, false, false, false, true), BackendKind::Hyprland);
}

// Under pkexec the desktop name is gone, and "kscreen-doctor is installed" promoted the
// session to KWin. A Hyprland (or GNOME/Sway) box with Plasma's tools installed got the KDE
// backend (VM-verified). Live compositor signals must win over that fallback.
TEST(DesktopBackend, LiveCompositorBeatsTheKscreenFallback) {
  EXPECT_EQ(select_backend_kind("", true, false, false, false, false, true), BackendKind::Hyprland);
  EXPECT_EQ(select_backend_kind("", true, false, false, false, true, false), BackendKind::Sway);
  EXPECT_EQ(select_backend_kind("", true, false, false, true, false, false), BackendKind::Gnome);
  EXPECT_EQ(select_backend_kind("", true, false, false), BackendKind::KWin);   // nothing else live
}

// Hyprland 0.5x defaults to a Lua config (hyprland.lua), where `hyprctl keyword` answers
// "unknown request" and settings go through `hyprctl eval '<lua>'` (VM-verified, 0.57.2).
TEST(HyprState, LuaMonitorRuleForPlacement) {
  std::vector<HyprMonitor> mons;
  ASSERT_TRUE(parse_hypr_monitors(kMonitors, mons));
  EXPECT_EQ(hypr_place_lua(mons, "DVI-I-1", 1600, 0),
            R"(hl.monitor({output="DVI-I-1", mode="1280x800@59.91", position="1600x0", scale=1, transform=1}))");
  EXPECT_EQ(hypr_place_lua(mons, "HDMI-9", 0, 0), "");
}
TEST(HyprState, LuaMonitorRuleForMirror) {
  std::vector<HyprMonitor> mons;
  ASSERT_TRUE(parse_hypr_monitors(kMonitors, mons));
  EXPECT_EQ(hypr_mirror_lua(mons, "DVI-I-1", "eDP-1"),
            R"(hl.monitor({output="DVI-I-1", mode="1280x800@59.91", position="auto", scale=1, mirror="eDP-1"}))");
}
TEST(HyprState, LuaDeviceOutput) {
  EXPECT_EQ(hypr_device_lua("droppix-touch-27000", "DVI-I-1"),
            R"(hl.device({name="droppix-touch-27000", output="DVI-I-1"}))");
  EXPECT_EQ(hypr_device_lua("bad\"name", "DVI-I-1"), "");
}
