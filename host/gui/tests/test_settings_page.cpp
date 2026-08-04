#include <gtest/gtest.h>
#include <QApplication>
#include <QSignalSpy>
#include <QSpinBox>
#include <QRadioButton>
#include "pages/settings_page.h"
#include "settings.h"

namespace {
// SettingsPage is a real QWidget; Qt calls qFatal() if a QWidget is constructed
// with no QApplication instance alive in the process. droppix_gui_tests links
// GTest::gtest_main (no Qt-aware main of its own), so bootstrap one lazily —
// guarded so future page-widget tests sharing this binary can call it too
// without trying to create a second QApplication. (Same pattern as
// test_about_page.cpp's ensureQApplication().)
void ensureQApplication() {
  if (QApplication::instance()) return;
  static int argc = 1;
  static char arg0[] = "droppix_gui_tests";
  static char* argv[] = {arg0, nullptr};
  static QApplication app(argc, argv);
}
}  // namespace

using namespace droppix;

TEST(SettingsPage, RoundTripsIncludingSurfacedFields) {
  ensureQApplication();
  SettingsPage page;
  Settings in; in.bitrate_kbps = 16000; in.port = 34000; in.touch = true;
  in.audio = true; in.orientation = 90; in.fps = 60; in.width = 1920; in.height = 1080;
  page.load(in);
  Settings out; page.store(out);
  EXPECT_EQ(out.bitrate_kbps, 16000);
  EXPECT_EQ(out.port, 34000);
  EXPECT_TRUE(out.touch);
  EXPECT_TRUE(out.audio);
  EXPECT_EQ(out.orientation, 90);
  EXPECT_EQ(out.fps, 60);
  EXPECT_EQ(out.width, 1920);
  EXPECT_EQ(out.height, 1080);
}

TEST(SettingsPage, EmitsSettingsChangedOnStreamerControl) {
  ensureQApplication();
  droppix::SettingsPage page;
  QSignalSpy spy(&page, &droppix::SettingsPage::settingsChanged);
  auto* bitrate = page.findChild<QSpinBox*>("bitrateSpin");
  ASSERT_NE(bitrate, nullptr);
  bitrate->setValue(bitrate->value() + 1000);
  EXPECT_GE(spy.count(), 1);
}

TEST(SettingsPage, AppPrefControlsDoNotEmitSettingsChanged) {
  ensureQApplication();
  droppix::SettingsPage page;
  QSignalSpy spy(&page, &droppix::SettingsPage::settingsChanged);
  auto* dark = page.findChild<QRadioButton*>("themeDark");
  ASSERT_NE(dark, nullptr);
  dark->setChecked(false);  // themeDark_ starts checked; this is a real true->false
                             // transition (and flips themeLight_ true via the radio
                             // group), so either theme radio being wired would trip the spy
  EXPECT_EQ(spy.count(), 0);
}

TEST(SettingsPage, LoadDoesNotEmitSettingsChanged) {
  ensureQApplication();
  droppix::SettingsPage page;
  QSignalSpy spy(&page, &droppix::SettingsPage::settingsChanged);
  droppix::Settings s; s.bitrate_kbps = 16000; s.port = 34000; s.webClient = true;
  s.touch = true; s.audio = true; s.orientation = 90; s.fps = 60; s.refresh_hz = 30;
  page.load(s);
  EXPECT_EQ(spy.count(), 0);
}
