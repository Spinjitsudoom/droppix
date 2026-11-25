#include "cosmic_state.h"
#include "desktop_backend.h"
#include <gtest/gtest.h>

using namespace droppix;

// `cosmic-randr list --kdl` (COSMIC 1.11), mode lists trimmed.
static const char* kKdl = R"(output "Virtual-1" enabled=#true {
  description make="Red Hat, Inc." model="QEMU Monitor"
  physical 320 200
  position 1280 0
  scale 1.00
  transform "normal"
  adaptive_sync_support #false
  adaptive_sync #false
  xwayland_primary #true
  modes {
    mode 5120 2160 50000
    mode 1280 800 74994 current=#true preferred=#true
  }
}
output "DVI-I-1" enabled=#true {
  description make="DpiX, Inc." model="droppix"
  physical 330 210
  position 0 0
  scale 2.00
  transform "90"
  xwayland_primary #false
  serial_number "0x00006978"
  modes {
    mode 2560 1600 59910 current=#true preferred=#true
  }
}
output "HDMI-A-1" enabled=#false {
  description make="Dell" model="U2720Q"
  modes {
    mode 3840 2160 60000
  }
}
)";

TEST(CosmicState, ParsesOutputsWithLayoutSize) {
  std::vector<CosmicOutput> outs;
  ASSERT_TRUE(parse_cosmic_outputs(kKdl, outs));
  ASSERT_EQ(outs.size(), 3u);
  EXPECT_EQ(outs[0].info.name, "Virtual-1");
  EXPECT_TRUE(outs[0].info.primary);          // xwayland_primary
  EXPECT_EQ(outs[0].info.geom.x, 1280);
  EXPECT_EQ(outs[0].info.geom.w, 1280);
  EXPECT_EQ(outs[1].serial, "0x00006978");
  EXPECT_EQ(outs[1].info.geom.w, 800);        // 2560 / 2, rotated 90°
  EXPECT_EQ(outs[1].info.geom.h, 1280);
  EXPECT_TRUE(outs[1].info.enabled);
  EXPECT_FALSE(outs[2].info.enabled);
}
TEST(CosmicState, FindsTheSessionOutputBySerial) {
  std::vector<CosmicOutput> outs;
  ASSERT_TRUE(parse_cosmic_outputs(kKdl, outs));
  EXPECT_EQ(cosmic_find_by_serial(outs, 27000), "DVI-I-1");
  EXPECT_EQ(cosmic_find_by_serial(outs, 27001), "");
}
TEST(CosmicState, RejectsNonKdl) {
  std::vector<CosmicOutput> outs;
  EXPECT_FALSE(parse_cosmic_outputs("Error: WaylandConnection(NoCompositor)", outs));
}

// cosmic-comp maps a device to an output with `map_to_output` in its per-device input config
// (RON, ~/.config/cosmic/com.system76.CosmicComp/v1/input_devices). That file is the user's,
// so droppix rewrites only its own "droppix-…" entries.
TEST(CosmicInput, WritesAFreshFile) {
  EXPECT_EQ(cosmic_input_devices("", "droppix-touch-27000", "DVI-I-1"),
            "{\n"
            "    \"droppix-touch-27000\": (state: Enabled, map_to_output: Some(\"DVI-I-1\")),\n"
            "}\n");
}
TEST(CosmicInput, KeepsTheUsersOwnEntriesAndReplacesOurs) {
  const std::string existing =
      "{\n"
      "    \"Logitech MX Master 3\": (\n"
      "        state: Enabled,\n"
      "        acceleration: Some((profile: Some(Flat), speed: 0.5)),\n"
      "    ),\n"
      "    \"droppix-touch-27000\": (state: Enabled, map_to_output: Some(\"DVI-I-2\")),\n"
      "    \"droppix-touch-27001\": (state: Enabled, map_to_output: Some(\"DVI-I-3\")),\n"
      "}\n";
  const std::string out = cosmic_input_devices(existing, "droppix-touch-27000", "DVI-I-1");
  EXPECT_NE(out.find("\"Logitech MX Master 3\": (\n        state: Enabled,\n"
                     "        acceleration: Some((profile: Some(Flat), speed: 0.5)),\n    )"),
            std::string::npos);
  EXPECT_NE(out.find("\"droppix-touch-27000\": (state: Enabled, map_to_output: Some(\"DVI-I-1\"))"),
            std::string::npos);
  EXPECT_EQ(out.find("DVI-I-2"), std::string::npos);
  // another session's entry is kept: only the same device is replaced
  EXPECT_NE(out.find("\"droppix-touch-27001\""), std::string::npos);
}
TEST(CosmicInput, RefusesUnsafeNamesAndUnparseableFiles) {
  EXPECT_EQ(cosmic_input_devices("", "bad\"name", "DVI-I-1"), "");
  EXPECT_EQ(cosmic_input_devices("{ \"x\": (state: Enabled", "droppix-touch-1", "DVI-I-1"), "");
}

TEST(DesktopBackend, CosmicSelectsCosmic) {
  EXPECT_EQ(select_backend_kind("COSMIC", false, false, false), BackendKind::Cosmic);
  EXPECT_EQ(select_backend_kind("", true, false, false, false, false, false, true), BackendKind::Cosmic);
}

// COSMIC saves output arrangements (~/.local/state/cosmic-comp/outputs.ron): after one mirror
// session the droppix monitor came back mirroring in every later session, and a mirroring
// output isn't in the layout, so touch mapped to it went nowhere (VM-verified).
static const char* kMirrored = R"(output "Virtual-1" enabled=#true {
  position 0 0
  scale 1.00
  xwayland_primary #true
  modes {
    mode 1280 800 74994 current=#true preferred=#true
  }
}
output "DVI-I-1" enabled=#true {
  position 1280 0
  scale 1.00
  mirroring "Virtual-1"
  serial_number "0x00006978"
  modes {
    mode 1280 800 59910 preferred=#true
  }
}
)";

TEST(CosmicState, AMirroringOutputTakesItsSourcesPlace) {
  std::vector<CosmicOutput> outs;
  ASSERT_TRUE(parse_cosmic_outputs(kMirrored, outs));
  EXPECT_EQ(outs[1].mirroring, "Virtual-1");
  EXPECT_TRUE(outs[1].info.enabled);
  EXPECT_EQ(outs[1].info.geom.x, 0);       // shows the source's area
  EXPECT_EQ(outs[1].info.geom.w, 1280);
}
TEST(CosmicState, TouchOnAMirrorMapsToItsSource) {
  std::vector<CosmicOutput> outs;
  ASSERT_TRUE(parse_cosmic_outputs(kMirrored, outs));
  EXPECT_EQ(cosmic_touch_target(outs, "DVI-I-1"), "Virtual-1");
  ASSERT_TRUE(parse_cosmic_outputs(kKdl, outs));
  EXPECT_EQ(cosmic_touch_target(outs, "DVI-I-1"), "DVI-I-1");
}
