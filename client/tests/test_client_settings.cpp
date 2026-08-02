#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QSettings>
#include "client_settings.h"
#include "keycode_util.h"
using namespace droppix;

TEST(ClientSettings, RotationToCode) {
  EXPECT_EQ(rotation_to_code(0), 0);   EXPECT_EQ(rotation_to_code(90), 1);
  EXPECT_EQ(rotation_to_code(180), 2); EXPECT_EQ(rotation_to_code(270), 3);
  EXPECT_EQ(rotation_to_code(45), 0);  // invalid -> 0
}
TEST(ClientSettings, SaveLoadRoundTrip) {
  static int argc = 0; static QCoreApplication app(argc, nullptr);
  QSettings::setDefaultFormat(QSettings::IniFormat);   // avoid touching the real config
  ClientSettings s; s.width=1280; s.height=720; s.fps=30; s.audio=true; s.rotation=90;
  ClientSettingsStore::save(s);
  ClientSettings r = ClientSettingsStore::load();
  EXPECT_EQ(r.width,1280); EXPECT_EQ(r.height,720); EXPECT_EQ(r.fps,30);
  EXPECT_TRUE(r.audio); EXPECT_EQ(r.rotation,90);
}
TEST(ClientSettings, BitrateDefaultAndRoundTrip) {
  static int argc = 0; static QCoreApplication app(argc, nullptr);
  QSettings::setDefaultFormat(QSettings::IniFormat);   // avoid touching the real config
  droppix::ClientSettings s; EXPECT_EQ(s.bitrate_kbps, 8000);
  s.bitrate_kbps = 16000; droppix::ClientSettingsStore::save(s);
  EXPECT_EQ(droppix::ClientSettingsStore::load().bitrate_kbps, 16000);
}
TEST(ClientSettings, FlipDefaultAndRoundTrip) {
  static int argc = 0; static QCoreApplication app(argc, nullptr);
  QSettings::setDefaultFormat(QSettings::IniFormat);   // avoid touching the real config
  droppix::ClientSettings s; EXPECT_FALSE(s.flip_horizontal);
  s.flip_horizontal = true; droppix::ClientSettingsStore::save(s);
  EXPECT_TRUE(droppix::ClientSettingsStore::load().flip_horizontal);
}
TEST(ClientSettings, BrightnessContrastDefaultAndRoundTrip) {
  static int argc = 0; static QCoreApplication app(argc, nullptr);
  QSettings::setDefaultFormat(QSettings::IniFormat);   // avoid touching the real config
  droppix::ClientSettings s;
  EXPECT_EQ(s.brightness, 0); EXPECT_EQ(s.contrast, 100);
  s.brightness = -40; s.contrast = 150; droppix::ClientSettingsStore::save(s);
  const droppix::ClientSettings r = droppix::ClientSettingsStore::load();
  EXPECT_EQ(r.brightness, -40); EXPECT_EQ(r.contrast, 150);
}
TEST(ClientSettings, WallDefaultAndRoundTrip) {
  static int argc = 0; static QCoreApplication app(argc, nullptr);
  QSettings::setDefaultFormat(QSettings::IniFormat);   // avoid touching the real config
  droppix::ClientSettings s; EXPECT_EQ(s.wall_col, 0); EXPECT_EQ(s.wall_row, 0);
  s.wall_col = 2; s.wall_row = 1; droppix::ClientSettingsStore::save(s);
  const droppix::ClientSettings r = droppix::ClientSettingsStore::load();
  EXPECT_EQ(r.wall_col, 2); EXPECT_EQ(r.wall_row, 1);
}
TEST(ScancodeToEvdev, X11SubtractsEight) {
  EXPECT_EQ(droppix::scancode_to_evdev(38, false), 30);  // X11 code 38 -> KEY_A(30)
  EXPECT_EQ(droppix::scancode_to_evdev(9,  false), 1);   // X11 code 9  -> KEY_ESC(1)
}
TEST(ScancodeToEvdev, WaylandNoOffset) {
  EXPECT_EQ(droppix::scancode_to_evdev(30, true), 30);   // wayland: already evdev
  EXPECT_EQ(droppix::scancode_to_evdev(1,  true), 1);
}
TEST(ScancodeToEvdev, RejectsTooLow) {
  EXPECT_EQ(droppix::scancode_to_evdev(8, false), 0);
  EXPECT_EQ(droppix::scancode_to_evdev(0, true), 0);
}
