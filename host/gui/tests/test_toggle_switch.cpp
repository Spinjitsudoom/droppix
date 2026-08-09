#include <gtest/gtest.h>
#include <QApplication>
#include "toggle_switch.h"

namespace {
// ToggleSwitch is a real QWidget; Qt calls qFatal() if one is constructed with no
// QApplication instance alive in the process. droppix_gui_tests links GTest::gtest_main
// (no Qt-aware main of its own), so bootstrap one lazily — guarded so other page/widget
// tests sharing this binary (see test_about_page.cpp) can call it too without trying to
// create a second QApplication.
void ensureQApplication() {
  if (QApplication::instance()) return;
  static int argc = 1;
  static char arg0[] = "droppix_gui_tests";
  static char* argv[] = {arg0, nullptr};
  static QApplication app(argc, argv);
}
}  // namespace

TEST(ToggleSwitch, DefaultsToOff) {
  ensureQApplication();
  droppix::ToggleSwitch sw;
  EXPECT_FALSE(sw.isChecked());
}

TEST(ToggleSwitch, ClickTogglesChecked) {
  ensureQApplication();
  droppix::ToggleSwitch sw;
  sw.click();
  EXPECT_TRUE(sw.isChecked());
  sw.click();
  EXPECT_FALSE(sw.isChecked());
}

TEST(ToggleSwitch, SetCheckedProgrammaticallyWorks) {
  ensureQApplication();
  droppix::ToggleSwitch sw;
  sw.setChecked(true);
  EXPECT_TRUE(sw.isChecked());
}

TEST(ToggleSwitch, SizeHintIsCompactPill) {
  ensureQApplication();
  droppix::ToggleSwitch sw;
  const QSize s = sw.sizeHint();
  EXPECT_GT(s.width(), s.height());   // wider than tall -- a horizontal pill, not square
  EXPECT_GT(s.height(), 0);
}
