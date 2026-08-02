#include <gtest/gtest.h>
#include <cstdio>
#include <filesystem>
#include "theme_pref.h"
using droppix::Theme; using droppix::loadThemePref; using droppix::saveThemePref;

static std::string tmpdir() {
  auto d = std::filesystem::temp_directory_path() / ("droppix-theme-" + std::to_string(::getpid()));
  std::filesystem::create_directories(d);
  return d.string();
}
TEST(ThemePref, DefaultsToDarkWhenAbsent) {
  EXPECT_EQ(loadThemePref(tmpdir()), Theme::Dark);
}
TEST(ThemePref, RoundTripsLight) {
  auto d = tmpdir(); saveThemePref(d, Theme::Light);
  EXPECT_EQ(loadThemePref(d), Theme::Light);
}
TEST(ThemePref, RoundTripsDark) {
  auto d = tmpdir(); saveThemePref(d, Theme::Dark);
  EXPECT_EQ(loadThemePref(d), Theme::Dark);
}
TEST(ThemePref, GarbageFallsBackToDark) {
  auto d = tmpdir(); FILE* f = std::fopen((d + "/theme").c_str(), "w"); std::fputs("purple", f); std::fclose(f);
  EXPECT_EQ(loadThemePref(d), Theme::Dark);
}
