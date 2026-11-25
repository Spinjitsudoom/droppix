#include "json_value.h"
#include "sway_state.h"
#include "desktop_backend.h"
#include <gtest/gtest.h>

using namespace droppix;

// ---- JSON ----

TEST(JsonValue, ParsesObjectsArraysAndScalars) {
  JVal v;
  ASSERT_TRUE(parse_json(R"({"a": [1, -2.5, true, false, null], "s": "q\"\\\/éx", "o": {}})", v));
  ASSERT_EQ(v.kind, JVal::Object);
  const JVal* a = v.get("a");
  ASSERT_NE(a, nullptr);
  ASSERT_EQ(a->items.size(), 5u);
  EXPECT_EQ(a->items[0].num, 1);
  EXPECT_DOUBLE_EQ(a->items[1].num, -2.5);
  EXPECT_TRUE(a->items[2].b);
  EXPECT_EQ(a->items[4].kind, JVal::Null);
  EXPECT_EQ(v.get("s")->str, "q\"\\/\xc3\xa9x");
  EXPECT_EQ(v.get("o")->kind, JVal::Object);
  EXPECT_EQ(v.get("nope"), nullptr);
}
TEST(JsonValue, RejectsMalformedText) {
  JVal v;
  EXPECT_FALSE(parse_json("", v));
  EXPECT_FALSE(parse_json("[1, 2", v));
  EXPECT_FALSE(parse_json("{\"a\" 1}", v));
  EXPECT_FALSE(parse_json("\"open", v));
  EXPECT_FALSE(parse_json("[1] trailing", v));
}

// ---- swaymsg -t get_outputs / get_inputs (captured from Sway 1.9) ----

static const char* kOutputs =
  R"([{"name":"Virtual-1","make":"Red Hat, Inc.","model":"QEMU Monitor","serial":"Unknown","active":true,"primary":false,"scale":1.0,"transform":"normal","rect":{"x":0,"y":0,"width":1280,"height":800},"current_mode":{"width":1280,"height":800,"refresh":74994,"picture_aspect_ratio":"none"}},)"
  R"({"name":"DVI-I-1","make":"DpiX, Inc.","model":"droppix","serial":"0x00006978","active":true,"primary":false,"scale":2.0,"transform":"normal","rect":{"x":1280,"y":0,"width":640,"height":400},"current_mode":{"width":1280,"height":800,"refresh":59910,"picture_aspect_ratio":"none"}},)"
  R"({"name":"HDMI-A-1","make":"Dell","model":"U2720Q","serial":"ABC","active":false,"primary":false,"rect":{"x":0,"y":0,"width":0,"height":0}}])";

static const char* kInputs =
  R"([{"identifier":"4617:58657:droppix-touch-27000","name":"droppix-touch-27000","vendor":4617,"product":58657,"type":"touch"},)"
  R"({"identifier":"4617:55043:droppix-keyboard","name":"droppix-keyboard","vendor":4617,"product":55043,"type":"keyboard"},)"
  R"({"identifier":"4617:58660:droppix-touch-27000-pen","name":"droppix-touch-27000-pen","vendor":4617,"product":58660,"type":"tablet_tool"}])";

TEST(SwayState, OutputsUseTheLayoutRect) {
  std::vector<SwayOutput> outs;
  ASSERT_TRUE(parse_sway_outputs(kOutputs, outs));
  ASSERT_EQ(outs.size(), 3u);
  EXPECT_EQ(outs[1].info.name, "DVI-I-1");
  EXPECT_TRUE(outs[1].info.enabled);
  EXPECT_EQ(outs[1].info.geom.x, 1280);
  EXPECT_EQ(outs[1].info.geom.w, 640);     // already scaled by Sway (2.0)
  EXPECT_EQ(outs[1].serial, "0x00006978");
  EXPECT_FALSE(outs[2].info.enabled);
}
TEST(SwayState, FindsTheSessionOutputBySerial) {
  std::vector<SwayOutput> outs;
  ASSERT_TRUE(parse_sway_outputs(kOutputs, outs));
  EXPECT_EQ(sway_find_by_serial(outs, 27000), "DVI-I-1");
  EXPECT_EQ(sway_find_by_serial(outs, 27001), "");
}
TEST(SwayState, RejectsNonArrayOutput) {
  std::vector<SwayOutput> outs;
  EXPECT_FALSE(parse_sway_outputs(R"({"success":false,"error":"x"})", outs));
}
TEST(SwayState, InputIdentifierByDeviceName) {
  EXPECT_EQ(sway_input_identifier(kInputs, "droppix-touch-27000"), "4617:58657:droppix-touch-27000");
  EXPECT_EQ(sway_input_identifier(kInputs, "droppix-touch-27000-pen"), "4617:58660:droppix-touch-27000-pen");
  EXPECT_EQ(sway_input_identifier(kInputs, "droppix-touch-27001"), "");
}
TEST(SwayState, IdentifierWithShellCharactersIsRefused) {
  const char* evil = R"([{"identifier":"1:2:x';reboot;'","name":"droppix-touch-1","type":"touch"}])";
  EXPECT_EQ(sway_input_identifier(evil, "droppix-touch-1"), "");
}
TEST(SwayState, PicksTheLiveSwaySocket) {
  const std::vector<std::string> entries = {"sway-ipc.1000.14437.sock", "sway-ipc.1000.14753.sock",
                                            "sway-ipc.1000.14878.sock", "wayland-1", "bus"};
  EXPECT_EQ(pick_sway_socket(entries, 1000, [](long pid) { return pid == 14753; }),
            "sway-ipc.1000.14753.sock");
  EXPECT_EQ(pick_sway_socket(entries, 1000, [](long) { return false; }), "");
  EXPECT_EQ(pick_sway_socket(entries, 1001, [](long) { return true; }), "");   // another user's
}

// ---- shared helpers ----

TEST(EdidSerial, MatchesHexOrDecimalText) {
  EXPECT_TRUE(edid_serial_matches("27000", 27000));
  EXPECT_TRUE(edid_serial_matches("0x00006978", 27000));
  EXPECT_FALSE(edid_serial_matches("Unknown", 27000));
  EXPECT_FALSE(edid_serial_matches("", 0));
  EXPECT_FALSE(edid_serial_matches("27000x", 27000));
}

TEST(WaylandSession, AnyLiveWaylandSocketMeansWayland) {
  EXPECT_TRUE(has_live_wayland_socket({"wayland-1", "wayland-1.lock", "bus"}));
  EXPECT_TRUE(has_live_wayland_socket({"wayland-0", "wayland-0.lock"}));
  EXPECT_FALSE(has_live_wayland_socket({"wayland-0", "bus"}));    // stale: no lock
  EXPECT_FALSE(has_live_wayland_socket({"bus", "pulse"}));
}

TEST(DesktopBackend, SwaySelectsSway) {
  EXPECT_EQ(select_backend_kind("sway", false, false, false), BackendKind::Sway);
  EXPECT_EQ(select_backend_kind("", false, false, false, false, true), BackendKind::Sway);
}
TEST(DesktopBackend, SwayIsNotMistakenForX11ByItsXwayland) {
  // x11_session is false whenever a live Wayland socket exists, even with XWayland's X0.
  EXPECT_EQ(select_backend_kind("", false, false, true, false, true), BackendKind::Sway);
}

TEST(SwayState, PlacingPinsEveryOtherActiveOutputWhereItIs) {
  // Sway auto-arranges outputs that have no explicit position AFTER the positioned ones, so
  // positioning only the droppix output pushed the main screen to its right, a bit further
  // every session. Pin the others in place in the same command.
  std::vector<SwayOutput> outs;
  ASSERT_TRUE(parse_sway_outputs(kOutputs, outs));
  EXPECT_EQ(sway_place_command(outs, "DVI-I-1", 1280, 0),
            "output Virtual-1 pos 0 0; output DVI-I-1 pos 1280 0");
}
TEST(SwayState, PlacingAnUnknownOrUnsafeOutputGivesNothing) {
  std::vector<SwayOutput> outs;
  ASSERT_TRUE(parse_sway_outputs(kOutputs, outs));
  EXPECT_EQ(sway_place_command(outs, "HDMI-A-9", 0, 0), "");
  EXPECT_EQ(sway_place_command(outs, "DVI;exit", 0, 0), "");
}

TEST(WaylandSession, SessionEnvDeclaresWaylandToQtTools) {
  // Plasma 5's Qt 5 kscreen-doctor defaulted to xcb under the root streamer, couldn't open
  // XWayland (no Xauthority) and printed nothing: droppix never found its output there.
  EXPECT_EQ(wayland_session_env({"wayland-0", "wayland-0.lock"}),
            "XDG_SESSION_TYPE=wayland QT_QPA_PLATFORM='wayland;xcb' ");
  EXPECT_EQ(wayland_session_env({"bus"}), "");   // X11 session: leave Qt's default alone
}

// A compositor leaves wayland-N and wayland-N.lock behind when it exits; only a running one
// holds the flock. An X11 session after a Wayland one was taken for Wayland (VM-verified).
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <cstdlib>
TEST(WaylandSession, OnlyHeldLocksCountAsLive) {
  char tmpl[] = "/tmp/droppix-wl-XXXXXX";
  ASSERT_NE(mkdtemp(tmpl), nullptr);
  const std::string dir = tmpl;
  for (const char* f : {"wayland-0", "wayland-0.lock", "wayland-1", "wayland-1.lock", "bus"})
    close(open((dir + "/" + f).c_str(), O_CREAT | O_WRONLY, 0600));
  const int held = open((dir + "/wayland-1.lock").c_str(), O_RDWR);
  ASSERT_EQ(flock(held, LOCK_EX | LOCK_NB), 0);   // wayland-1 is "running"; wayland-0 is stale
  const auto live = live_runtime_entries(dir);
  EXPECT_TRUE(has_live_wayland_socket(live));
  EXPECT_EQ(pick_wayland_display(live), "wayland-1");
  close(held);
  EXPECT_FALSE(has_live_wayland_socket(live_runtime_entries(dir)));   // nothing running now
  std::system(("rm -rf " + dir).c_str());
}
