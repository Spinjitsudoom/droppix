#include <gtest/gtest.h>
#include <QTemporaryDir>
#include "profile_store.h"

using namespace droppix;

TEST(ProfileStore, SaveLoadRoundTrip) {
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  ProfileStore store(tmp.path());

  Settings s;
  s.source = Settings::Source::Evdi;
  s.width = 1920; s.height = 1080; s.fps = 60; s.bitrate_kbps = 12000;
  s.port = 27123; s.auto_adb_reverse = false; s.refresh_hz = 30;
  ASSERT_TRUE(store.save("hi-q", s));

  Settings out;
  ASSERT_TRUE(store.load("hi-q", out));
  EXPECT_EQ(out.source, Settings::Source::Evdi);
  EXPECT_EQ(out.width, 1920); EXPECT_EQ(out.height, 1080);
  EXPECT_EQ(out.fps, 60); EXPECT_EQ(out.bitrate_kbps, 12000);
  EXPECT_EQ(out.port, 27123); EXPECT_FALSE(out.auto_adb_reverse);
  EXPECT_EQ(out.refresh_hz, 30);

  EXPECT_TRUE(store.names().contains("hi-q"));
  EXPECT_TRUE(store.remove("hi-q"));
  EXPECT_FALSE(store.load("hi-q", out));
}

TEST(ProfileStore, MissingProfileLoadFails) {
  QTemporaryDir tmp;
  ProfileStore store(tmp.path());
  Settings out;
  EXPECT_FALSE(store.load("nope", out));
}
