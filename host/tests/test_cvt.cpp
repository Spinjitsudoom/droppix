#include <gtest/gtest.h>
#include "cvt.h"
#include "edid.h"

using namespace droppix;

static void expectRb(const Timing& t, int clk, int vsync, int vblank,
                     int w, int h) {
  EXPECT_EQ(t.pixel_clock_khz, clk);
  EXPECT_EQ(t.h_active, (w / 8) * 8);
  EXPECT_EQ(t.h_front, 48); EXPECT_EQ(t.h_sync, 32); EXPECT_EQ(t.h_blank, 160);
  EXPECT_EQ(t.v_active, h);
  EXPECT_EQ(t.v_front, 3); EXPECT_EQ(t.v_sync, vsync); EXPECT_EQ(t.v_blank, vblank);
}

TEST(Cvt, Rb1080p60)  { expectRb(cvt_rb_timing(1920,1080,60), 138500, 5, 31, 1920,1080); }
TEST(Cvt, Rb720p60)   { expectRb(cvt_rb_timing(1280, 720,60),  64000, 5, 21, 1280, 720); }
TEST(Cvt, Rb1600p60)  { expectRb(cvt_rb_timing(2560,1600,60), 268500, 6, 46, 2560,1600); }
TEST(Cvt, Rb1200p60)  { expectRb(cvt_rb_timing(1920,1200,60), 154000, 6, 35, 1920,1200); }
TEST(Cvt, Rb1080p30)  { expectRb(cvt_rb_timing(1920,1080,30),  68250, 5, 16, 1920,1080); }
// Low-res modes (sub-720 GUI presets) must also produce valid reduced-blanking timings.
TEST(Cvt, Rb480p60)   { expectRb(cvt_rb_timing( 640, 480,60),  23500, 4, 14,  640, 480); }
TEST(Cvt, Rb600p60)   { expectRb(cvt_rb_timing( 800, 600,60),  35500, 4, 18,  800, 600); }
TEST(Cvt, Rb540p60)   { expectRb(cvt_rb_timing( 960, 540,60),  37250, 5, 16,  960, 540); }

TEST(Cvt, ModeTimingUsesCeaPresetFor1080p60) {
  // The default 1080p60 must use the verified CEA preset (148500), not CVT (138500).
  EXPECT_EQ(mode_timing(1920,1080,60).pixel_clock_khz, 148500);
}
TEST(Cvt, ModeTimingUsesCvtForOtherModes) {
  EXPECT_EQ(mode_timing(1280,720,60).pixel_clock_khz, 64000);
}
TEST(Cvt, BuildEdidOfCvtEncodesActivePixels) {
  auto e = build_edid(cvt_rb_timing(2560,1600,60));
  ASSERT_EQ(e.size(), 128u);
  const int o = 54;  // DTD #1
  int h_active = e[o+2] | ((e[o+4] & 0xF0) << 4);
  int v_active = e[o+5] | ((e[o+7] & 0xF0) << 4);
  EXPECT_EQ(h_active, 2560);
  EXPECT_EQ(v_active, 1600);
}
