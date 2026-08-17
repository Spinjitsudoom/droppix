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
  s.port = 27123; s.auto_adb_reverse = false; s.refresh_hz = 30; s.touch = true;
  s.autoConnect = false;   // default is true; prove it round-trips
  ASSERT_TRUE(store.save("hi-q", s));

  Settings out;
  ASSERT_TRUE(store.load("hi-q", out));
  EXPECT_EQ(out.source, Settings::Source::Evdi);
  EXPECT_EQ(out.width, 1920); EXPECT_EQ(out.height, 1080);
  EXPECT_EQ(out.fps, 60); EXPECT_EQ(out.bitrate_kbps, 12000);
  EXPECT_EQ(out.port, 27123); EXPECT_FALSE(out.auto_adb_reverse);
  EXPECT_EQ(out.refresh_hz, 30);
  EXPECT_TRUE(out.touch);
  EXPECT_FALSE(out.autoConnect);

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

TEST(ProfileStore, LastUsedRoundTrip) {
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  EXPECT_TRUE(ProfileStore(tmp.path()).lastUsed().isEmpty());  // none yet
  ProfileStore(tmp.path()).setLastUsed("hi-q");
  // A fresh instance (simulating a restart) must read the persisted value.
  EXPECT_EQ(ProfileStore(tmp.path()).lastUsed(), QString("hi-q"));
}

// The working settings must persist WITHOUT an explicit profile Save. Ticking "Web client"
// and closing used to lose it: profiles are only written by Save/Save As, and nothing
// captured the live state, so the next launch restored an older profile (or defaults).
TEST(ProfileStore, SessionRoundTripsWithoutANamedProfile) {
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  ProfileStore store(tmp.path());

  Settings s;
  s.webClient = true;        // the setting that prompted this
  s.audio = false;           // default is true; prove a flip round-trips both ways
  s.port = 27500;
  s.fps = 24;
  ASSERT_TRUE(store.saveSession(s));

  Settings out;
  ASSERT_TRUE(store.loadSession(out));
  EXPECT_TRUE(out.webClient);
  EXPECT_FALSE(out.audio);
  EXPECT_EQ(out.port, 27500);
  EXPECT_EQ(out.fps, 24);

  EXPECT_TRUE(store.names().isEmpty()) << "the session must not appear as a named profile";
}

TEST(ProfileStore, NoSessionYetIsNotAnError) {
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  ProfileStore store(tmp.path());
  Settings out;
  EXPECT_FALSE(store.loadSession(out)) << "a first launch must fall back, not fail loudly";
}

// Saving the working state must never rewrite a profile the user saved deliberately.
TEST(ProfileStore, SessionDoesNotDisturbNamedProfiles) {
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  ProfileStore store(tmp.path());

  Settings profile;
  profile.webClient = false;
  profile.fps = 60;
  ASSERT_TRUE(store.save("desk", profile));

  Settings session;
  session.webClient = true;
  session.fps = 24;
  ASSERT_TRUE(store.saveSession(session));

  Settings out;
  ASSERT_TRUE(store.load("desk", out));
  EXPECT_FALSE(out.webClient) << "the named profile must be untouched";
  EXPECT_EQ(out.fps, 60);
}
