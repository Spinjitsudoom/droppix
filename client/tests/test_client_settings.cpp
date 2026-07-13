#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QSettings>
#include "client_settings.h"
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
