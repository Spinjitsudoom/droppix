#include <gtest/gtest.h>
#include <QApplication>
#include <QLabel>
#include "pages/about_page.h"
#include "version.h"

namespace {
// AboutPage is a real QWidget; Qt calls qFatal() if a QWidget is constructed
// with no QApplication instance alive in the process. droppix_gui_tests links
// GTest::gtest_main (no Qt-aware main of its own), so bootstrap one lazily —
// guarded so future page-widget tests sharing this binary can call it too
// without trying to create a second QApplication.
void ensureQApplication() {
  if (QApplication::instance()) return;
  static int argc = 1;
  static char arg0[] = "droppix_gui_tests";
  static char* argv[] = {arg0, nullptr};
  static QApplication app(argc, argv);
}
}  // namespace

TEST(AboutPage, ShowsVersion) {
  ensureQApplication();
  droppix::AboutPage page;
  auto* v = page.findChild<QLabel*>("aboutVersion");
  ASSERT_NE(v, nullptr);
  // Reflects the build version (git describe, or the static fallback) rather than a
  // hardcoded literal, so it can't silently drift from the real build.
  EXPECT_EQ(v->text(), QString("Version %1").arg(droppix::app_version()));
  EXPECT_GT(v->text().length(), QStringLiteral("Version ").length());
}
