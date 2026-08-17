#include <gtest/gtest.h>

#include "aoa_presence.h"

using namespace droppix;

namespace {
QSet<QString> s(std::initializer_list<const char*> xs) {
  QSet<QString> out;
  for (const char* x : xs) out.insert(QString::fromLatin1(x));
  return out;
}
}  // namespace

// A tablet that is attached is never reported, however long it streams.
TEST(AoaPresence, AttachedIsNeverReportedGone) {
  AoaPresence p;
  for (int i = 0; i < 10; ++i)
    EXPECT_TRUE(p.update(s({"AAA"}), s({"AAA"})).isEmpty()) << "scan " << i;
}

// The whole point of the debounce: switching into accessory mode re-enumerates the device,
// and a flaky cable drops it repeatedly. A single missed scan must NOT kill the monitor.
TEST(AoaPresence, SingleMissedScanIsNotAnUnplug) {
  AoaPresence p;
  EXPECT_TRUE(p.update(s({"AAA"}), s({})).isEmpty());
  EXPECT_TRUE(p.update(s({"AAA"}), s({"AAA"})).isEmpty()) << "reappearing must clear the miss";
  EXPECT_EQ(p.misses("AAA"), 0);
}

TEST(AoaPresence, ReportsAfterConsecutiveMisses) {
  AoaPresence p(3);
  EXPECT_TRUE(p.update(s({"AAA"}), s({})).isEmpty());
  EXPECT_TRUE(p.update(s({"AAA"}), s({})).isEmpty());
  EXPECT_EQ(p.update(s({"AAA"}), s({})), s({"AAA"}));
}

// Misses must be CONSECUTIVE — a device that keeps flickering back is present, not gone.
TEST(AoaPresence, AlternatingNeverReportsGone) {
  AoaPresence p(3);
  for (int i = 0; i < 6; ++i) {
    EXPECT_TRUE(p.update(s({"AAA"}), s({})).isEmpty());
    EXPECT_TRUE(p.update(s({"AAA"}), s({"AAA"})).isEmpty());
  }
}

// Reported once only: the caller tears the session down on the first report, and a repeat
// would try to stop a session that no longer exists.
TEST(AoaPresence, ReportsEachSerialOnlyOnce) {
  AoaPresence p(2);
  p.update(s({"AAA"}), s({}));
  EXPECT_EQ(p.update(s({"AAA"}), s({})), s({"AAA"}));
  EXPECT_TRUE(p.update(s({"AAA"}), s({})).isEmpty()) << "must not report the same unplug twice";
}

TEST(AoaPresence, TracksSerialsIndependently) {
  AoaPresence p(2);
  p.update(s({"AAA", "BBB"}), s({"BBB"}));
  const auto gone = p.update(s({"AAA", "BBB"}), s({"BBB"}));
  EXPECT_EQ(gone, s({"AAA"}));
  EXPECT_EQ(p.misses("BBB"), 0);
}

// A session ending for any other reason (user pressed Stop, client sent BYE) must reset the
// tracking, so a later reconnect does not inherit a half-finished miss count.
TEST(AoaPresence, UntrackedSerialIsForgotten) {
  AoaPresence p(2);
  p.update(s({"AAA"}), s({}));
  EXPECT_EQ(p.misses("AAA"), 1);
  p.update(s({}), s({}));                       // session gone
  EXPECT_EQ(p.misses("AAA"), 0);
  EXPECT_TRUE(p.update(s({"AAA"}), s({})).isEmpty()) << "counting must restart from zero";
}
