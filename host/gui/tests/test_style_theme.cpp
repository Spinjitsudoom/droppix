#include <gtest/gtest.h>
#include "style.h"
using droppix::Theme; using droppix::styleSheet;

TEST(StyleTheme, DarkAndLightDiffer) {
  EXPECT_NE(styleSheet(Theme::Dark), styleSheet(Theme::Light));
}
TEST(StyleTheme, BothCarryAccentAndBaseSelectors) {
  for (auto t : {Theme::Dark, Theme::Light}) {
    const QString q = styleSheet(t);
    EXPECT_TRUE(q.contains("#14b8a6"));   // teal accent in both
    EXPECT_TRUE(q.contains("QWidget"));   // real stylesheet, not empty
    EXPECT_GT(q.size(), 400);
  }
}
TEST(StyleTheme, GroundsMatchTheme) {
  EXPECT_TRUE(styleSheet(Theme::Dark).contains("#1b1f24"));   // dark ground
  EXPECT_TRUE(styleSheet(Theme::Light).contains("#f"));        // a light (#f..) ground
}
