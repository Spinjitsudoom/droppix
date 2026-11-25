#include "gvariant_text.h"
#include "mutter_state.h"
#include <gtest/gtest.h>

using namespace droppix;

// ---- GVariant text (gdbus's print format) ----

TEST(GVariantText, ParsesScalarsWithTypePrefixes) {
  GVal v;
  ASSERT_TRUE(parse_gvariant_text("(uint32 7, -3, 1.5, true, 'it\\'s', \"dq\", byte 0x1f)", v));
  ASSERT_EQ(v.kind, GVal::Tuple);
  ASSERT_EQ(v.items.size(), 7u);
  EXPECT_EQ(v.items[0].num, 7);
  EXPECT_EQ(v.items[1].num, -3);
  EXPECT_DOUBLE_EQ(v.items[2].num, 1.5);
  EXPECT_TRUE(v.items[3].b);
  EXPECT_EQ(v.items[4].str, "it's");
  EXPECT_EQ(v.items[5].str, "dq");
  EXPECT_EQ(v.items[6].num, 31);
}

TEST(GVariantText, ParsesArraysDictsVariantsAndTypeAnnotations) {
  GVal v;
  ASSERT_TRUE(parse_gvariant_text("([1, 2], @a{sv} {}, {'k': <uint32 4>, 'n': <'x'>}, @as [])", v));
  ASSERT_EQ(v.items.size(), 4u);
  EXPECT_EQ(v.items[0].kind, GVal::Array);
  EXPECT_EQ(v.items[0].items.size(), 2u);
  EXPECT_EQ(v.items[1].kind, GVal::Dict);
  EXPECT_TRUE(v.items[1].entries.empty());
  const GVal* k = v.items[2].get("k");
  ASSERT_NE(k, nullptr);
  EXPECT_EQ(k->kind, GVal::Variant);
  EXPECT_EQ(k->items.at(0).num, 4);
  EXPECT_EQ(v.items[2].get("missing"), nullptr);
  EXPECT_EQ(v.items[3].kind, GVal::Array);
  EXPECT_TRUE(v.items[3].items.empty());
}

TEST(GVariantText, RejectsTruncatedText) {
  GVal v;
  EXPECT_FALSE(parse_gvariant_text("(uint32 7, [1, 2", v));
  EXPECT_FALSE(parse_gvariant_text("('unterminated)", v));
  EXPECT_FALSE(parse_gvariant_text("", v));
}

// ---- Mutter DisplayConfig.GetCurrentState ----

// One built-in 1920x1080 primary at 100 %, plus a droppix tablet 2560x1600 at 200 % to its right.
static const char* kTwoMonitors =
  "(uint32 12, "
  "[(('eDP-1', 'BOE', '0x0a2b', '0x00000000'), "
  "[('1920x1080@60.000', 1920, 1080, 60.0, 1.0, [1.0, 2.0], {'is-current': <true>, 'is-preferred': <true>}), "
  "('1280x720@60.000', 1280, 720, 60.0, 1.0, [1.0], @a{sv} {})], "
  "{'is-builtin': <true>, 'display-name': <'Built-in display'>}), "
  "(('DVI-I-1', 'DPX', 'droppix', '27000'), "
  "[('2560x1600@59.972', 2560, 1600, 59.97200012207031, 2.0, [1.0, 1.25, 1.5, 1.75, 2.0], {'is-current': <true>})], "
  "{'is-builtin': <false>, 'display-name': <'droppix'>})], "
  "[(0, 0, 1.0, uint32 0, true, [('eDP-1', 'BOE', '0x0a2b', '0x00000000')], @a{sv} {}), "
  "(1920, 0, 2.0, uint32 0, false, [('DVI-I-1', 'DPX', 'droppix', '27000')], @a{sv} {})], "
  "{'layout-mode': <uint32 1>, 'supports-changing-layout-mode': <false>})";

TEST(MutterState, ParsesMonitorsAndLogicalMonitors) {
  MutterState st;
  ASSERT_TRUE(parse_mutter_state(kTwoMonitors, st));
  EXPECT_EQ(st.serial, 12u);
  ASSERT_EQ(st.monitors.size(), 2u);
  EXPECT_EQ(st.monitors[1].connector, "DVI-I-1");
  EXPECT_EQ(st.monitors[1].vendor, "DPX");
  EXPECT_EQ(st.monitors[1].product, "droppix");
  EXPECT_EQ(st.monitors[1].serial, "27000");
  const MutterMode* m = st.monitors[1].current_mode();
  ASSERT_NE(m, nullptr);
  EXPECT_EQ(m->id, "2560x1600@59.972");
  EXPECT_EQ(m->w, 2560);
  ASSERT_EQ(st.logical.size(), 2u);
  EXPECT_EQ(st.logical[1].x, 1920);
  EXPECT_DOUBLE_EQ(st.logical[1].scale, 2.0);
  EXPECT_TRUE(st.logical[0].primary);
  EXPECT_EQ(st.logical[1].connectors, std::vector<std::string>{"DVI-I-1"});
  EXPECT_EQ(st.layout_mode, 1u);
}

TEST(MutterState, OutputsUseLogicalSizeUnderScaling) {
  MutterState st;
  ASSERT_TRUE(parse_mutter_state(kTwoMonitors, st));
  const auto outs = mutter_outputs(st);
  ASSERT_EQ(outs.size(), 2u);
  EXPECT_EQ(outs[0].name, "eDP-1");
  EXPECT_TRUE(outs[0].primary);
  EXPECT_EQ(outs[1].name, "DVI-I-1");
  EXPECT_TRUE(outs[1].enabled);
  EXPECT_EQ(outs[1].geom.x, 1920);
  EXPECT_EQ(outs[1].geom.w, 1280);   // 2560 / 2.0
  EXPECT_EQ(outs[1].geom.h, 800);
}

TEST(MutterState, PhysicalLayoutModeKeepsModeSize) {
  std::string t = kTwoMonitors;
  t.replace(t.find("'layout-mode': <uint32 1>"), 25, "'layout-mode': <uint32 2>");
  MutterState st;
  ASSERT_TRUE(parse_mutter_state(t, st));
  EXPECT_EQ(mutter_outputs(st)[1].geom.w, 2560);
}

TEST(MutterState, RotatedOutputSwapsWidthAndHeight) {
  std::string t = kTwoMonitors;
  t.replace(t.find("(1920, 0, 2.0, uint32 0"), 23, "(1920, 0, 2.0, uint32 1");
  MutterState st;
  ASSERT_TRUE(parse_mutter_state(t, st));
  const auto outs = mutter_outputs(st);
  EXPECT_EQ(outs[1].geom.w, 800);
  EXPECT_EQ(outs[1].geom.h, 1280);
}

TEST(MutterState, MonitorWithoutALogicalMonitorIsDisabled) {
  std::string t = kTwoMonitors;
  const auto at = t.find(", (1920, 0, 2.0");
  t.erase(at, t.find("@a{sv} {})]", at) + 10 - at);
  MutterState st;
  ASSERT_TRUE(parse_mutter_state(t, st));
  const auto outs = mutter_outputs(st);
  ASSERT_EQ(outs.size(), 2u);
  EXPECT_FALSE(outs[1].enabled);
}

TEST(MutterState, RejectsGarbage) {
  MutterState st;
  EXPECT_FALSE(parse_mutter_state("Error: GDBus.Error:org.freedesktop.DBus.Error.ServiceUnknown", st));
  EXPECT_FALSE(parse_mutter_state("(uint32 1,)", st));
}

TEST(MutterState, FindsTheMonitorWithASessionSerial) {
  MutterState st;
  ASSERT_TRUE(parse_mutter_state(kTwoMonitors, st));
  EXPECT_EQ(mutter_find_by_serial(st, 27000), "DVI-I-1");
  EXPECT_EQ(mutter_find_by_serial(st, 27001), "");
}

TEST(MutterState, SerialMatchAcceptsHexFormatting) {
  std::string t = kTwoMonitors;
  for (size_t p; (p = t.find("'27000'")) != std::string::npos;) t.replace(p, 7, "'0x00006978'");
  MutterState st;
  ASSERT_TRUE(parse_mutter_state(t, st));
  EXPECT_EQ(mutter_find_by_serial(st, 27000), "DVI-I-1");
}

// ---- ApplyMonitorsConfig ----

TEST(MutterApply, MovesOneOutputAndKeepsTheRest) {
  MutterState st;
  ASSERT_TRUE(parse_mutter_state(kTwoMonitors, st));
  const std::string a = mutter_place_config(st, "DVI-I-1", 1920, 200);
  EXPECT_EQ(a,
    "[(0, 0, 1.0, uint32 0, true, [('eDP-1', '1920x1080@60.000', @a{sv} {})]), "
    "(1920, 200, 2.0, uint32 0, false, [('DVI-I-1', '2560x1600@59.972', @a{sv} {})])]");
}

TEST(MutterApply, PlacingAnUnknownOutputGivesNothing) {
  MutterState st;
  ASSERT_TRUE(parse_mutter_state(kTwoMonitors, st));
  EXPECT_EQ(mutter_place_config(st, "HDMI-9", 0, 0), "");
}

TEST(MutterApply, MirrorNeedsACommonMode) {
  MutterState st;
  ASSERT_TRUE(parse_mutter_state(kTwoMonitors, st));
  EXPECT_EQ(mutter_mirror_config(st, "DVI-I-1"), "");   // no shared resolution
}

TEST(MutterApply, MirrorJoinsBothIntoThePrimaryLogicalMonitor) {
  std::string t = kTwoMonitors;
  // give the tablet a 1920x1080 mode too
  const std::string needle = "{'is-current': <true>})], {'is-builtin': <false>";
  t.replace(t.find(needle), needle.size(),
            "{'is-current': <true>}), ('1920x1080@60.000', 1920, 1080, 60.0, 1.0, [1.0], @a{sv} {})], "
            "{'is-builtin': <false>");
  MutterState st;
  ASSERT_TRUE(parse_mutter_state(t, st));
  EXPECT_EQ(mutter_mirror_config(st, "DVI-I-1"),
    "[(0, 0, 1.0, uint32 0, true, [('eDP-1', '1920x1080@60.000', @a{sv} {}), "
    "('DVI-I-1', '1920x1080@60.000', @a{sv} {})])]");
}

// ---- touch / stylus -> monitor (GNOME per-device gsettings) ----

TEST(GnomeMapCommand, TouchscreenPathUsesLowercaseHexIds) {
  MutterMonitor m; m.vendor = "DPX"; m.product = "droppix"; m.serial = "27000";
  EXPECT_EQ(gnome_map_command(false, 0xde81, m),
    "gsettings set org.gnome.desktop.peripherals.touchscreen:"
    "/org/gnome/desktop/peripherals/touchscreens/1209:de81/ output \"['DPX', 'droppix', '27000']\"");
}
TEST(GnomeMapCommand, PenUsesTheTabletSchema) {
  MutterMonitor m; m.vendor = "DPX"; m.product = "droppix"; m.serial = "0x00006978";
  EXPECT_EQ(gnome_map_command(true, 0xd704, m),
    "gsettings set org.gnome.desktop.peripherals.tablet:"
    "/org/gnome/desktop/peripherals/tablets/1209:d704/ output \"['DPX', 'droppix', '0x00006978']\"");
}
TEST(GnomeMapCommand, RefusesIdentityTextThatCouldEscapeTheCommand) {
  MutterMonitor m; m.vendor = "DPX"; m.product = "a'b"; m.serial = "1";
  EXPECT_EQ(gnome_map_command(false, 0xd701, m), "");
  m.product = "a\"$(x)"; EXPECT_EQ(gnome_map_command(false, 0xd701, m), "");
  m.product = "Dell U2720Q"; EXPECT_NE(gnome_map_command(false, 0xd701, m), "");   // spaces are fine
}

// ---- reviving a monitor GNOME thinks is already on (reconnect) ----

TEST(MutterApply, DisableDropsTheOutputAndKeepsTheRest) {
  MutterState st;
  ASSERT_TRUE(parse_mutter_state(kTwoMonitors, st));
  EXPECT_EQ(mutter_disable_config(st, "DVI-I-1"),
    "[(0, 0, 1.0, uint32 0, true, [('eDP-1', '1920x1080@60.000', @a{sv} {})])]");
}
TEST(MutterApply, DisablingTheOnlyOtherOutputOrAnUnknownOneGivesNothing) {
  MutterState st;
  ASSERT_TRUE(parse_mutter_state(kTwoMonitors, st));
  EXPECT_EQ(mutter_disable_config(st, "HDMI-9"), "");   // not active
}
TEST(MutterState, ParsesRealGnome46Output) {
  // Captured from Ubuntu 24.04 (GNOME 46) with a droppix session: note the second logical
  // monitor's bare "0" transform and the untyped legacy-ui-scaling-factor.
  const char* t =
    "(uint32 7, [(('Virtual-1', 'RHT', 'QEMU Monitor', '0x00000000'), "
    "[('1280x800@74.994', 1280, 800, 74.993850708007812, 1.0, [1.0], {'is-current': <true>, 'is-preferred': <true>})], "
    "{'is-builtin': <false>, 'display-name': <'Red Hat, Inc. 15\"'>}), "
    "(('DVI-I-1', 'DPX', 'droppix', '0x00006978'), "
    "[('1280x800@59.910', 1280, 800, 59.9095458984375, 1.0, [1.0], {'is-current': <true>, 'is-preferred': <true>})], "
    "{'is-builtin': <false>, 'display-name': <'DpiX, Inc. 15\"'>})], "
    "[(0, 0, 1.0, uint32 0, true, [('Virtual-1', 'RHT', 'QEMU Monitor', '0x00000000')], @a{sv} {}), "
    "(1280, 0, 1.0, 0, false, [('DVI-I-1', 'DPX', 'droppix', '0x00006978')], {})], "
    "{'renderer': <'native'>, 'layout-mode': <uint32 2>, 'legacy-ui-scaling-factor': <1>})";
  MutterState st;
  ASSERT_TRUE(parse_mutter_state(t, st));
  EXPECT_EQ(mutter_find_by_serial(st, 27000), "DVI-I-1");
  const auto outs = mutter_outputs(st);
  ASSERT_EQ(outs.size(), 2u);
  EXPECT_EQ(outs[1].geom.x, 1280);
  EXPECT_EQ(outs[1].geom.w, 1280);
  EXPECT_EQ(mutter_place_config(st, "DVI-I-1", 1280, 0),
    "[(0, 0, 1.0, uint32 0, true, [('Virtual-1', '1280x800@74.994', @a{sv} {})]), "
    "(1280, 0, 1.0, uint32 0, false, [('DVI-I-1', '1280x800@59.910', @a{sv} {})])]");
}
