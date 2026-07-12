#include <gtest/gtest.h>
#include "session_params.h"
using namespace droppix;

TEST(SessionParams, V4PrefersClientValues) {
  auto p = select_session_params(4, /*fps*/60, /*audio*/1, /*orient*/2,
                                 /*h_bitrate*/0, /*def_fps*/30, /*def_audio*/false, /*def_orient*/0, /*def_bitrate*/8000);
  EXPECT_EQ(p.fps, 60); EXPECT_TRUE(p.audio); EXPECT_EQ(p.orientation, 2); EXPECT_EQ(p.bitrate, 8000);
}
TEST(SessionParams, V4ZeroFpsFallsBackToDefault) {
  auto p = select_session_params(4, 0, 0, 0, 0, 30, true, 1, 8000);
  EXPECT_EQ(p.fps, 30);            // fps sentinel 0 -> default
  EXPECT_FALSE(p.audio);           // v4 audio flag is authoritative (client didn't ask)
  EXPECT_EQ(p.orientation, 0);
  EXPECT_EQ(p.bitrate, 8000);
}
TEST(SessionParams, PreV4UsesDefaults) {
  auto p = select_session_params(3, 60, 1, 2, 0, 24, true, 3, 8000);
  EXPECT_EQ(p.fps, 24); EXPECT_TRUE(p.audio); EXPECT_EQ(p.orientation, 3); EXPECT_EQ(p.bitrate, 8000);
}
TEST(SessionParams, OrientationMaskedToTwoBits) {
  auto p = select_session_params(4, 30, 0, 7, 0, 30, false, 0, 8000);
  EXPECT_EQ(p.orientation, 3);     // 7 & 3
  EXPECT_EQ(p.bitrate, 8000);
}

TEST(SessionParams, V5PrefersClientBitrate) {
  auto p = select_session_params(5, 60, 1, 1, 12000, 30, false, 0, 8000);
  EXPECT_EQ(p.bitrate, 12000); EXPECT_EQ(p.fps, 60);
}
TEST(SessionParams, V4HasNoBitrateFieldUsesDefault) {
  auto p = select_session_params(4, 60, 1, 1, 12000, 30, false, 0, 8000);
  EXPECT_EQ(p.bitrate, 8000);   // bitrate is v5-only; a v4 client's hello_bitrate is meaningless -> default
}
TEST(SessionParams, V5ZeroBitrateFallsBack) {
  auto p = select_session_params(5, 60, 1, 1, 0, 30, false, 0, 8000);
  EXPECT_EQ(p.bitrate, 8000);
}
