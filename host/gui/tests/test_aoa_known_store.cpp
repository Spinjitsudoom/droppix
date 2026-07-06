#include "aoa_known_store.h"
#include <gtest/gtest.h>
#include <QDir>
#include <QTemporaryDir>

using namespace droppix;

TEST(AoaKnownStore, AddContainsPersists) {
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  {
    AoaKnownStore s(tmp.path());
    EXPECT_FALSE(s.contains("R32D204ZH6J"));
    s.add("R32D204ZH6J");
    s.add("R32D204ZH6J");   // idempotent
    EXPECT_TRUE(s.contains("R32D204ZH6J"));
    EXPECT_EQ(s.all().size(), 1);
  }
  // A fresh store over the same dir reloads the persisted serial.
  AoaKnownStore reloaded(tmp.path());
  EXPECT_TRUE(reloaded.contains("R32D204ZH6J"));
}

TEST(AoaKnownStore, EmptyByDefault) {
  QTemporaryDir tmp;
  AoaKnownStore s(tmp.path());
  EXPECT_TRUE(s.all().isEmpty());
  EXPECT_FALSE(s.contains("anything"));
}
