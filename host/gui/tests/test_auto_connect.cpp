#include "auto_connect.h"
#include <gtest/gtest.h>

using namespace droppix;

static AutoConnectCandidate cand(const QString& k, bool e) {
  AutoConnectCandidate c; c.key = k; c.eligible = e; return c;
}

TEST(AutoConnect, DisabledReturnsEmpty) {
  EXPECT_TRUE(devicesToConnect(false, {cand("usb:A", true)}, {}).isEmpty());
}

TEST(AutoConnect, IneligibleSkipped) {
  EXPECT_TRUE(devicesToConnect(true, {cand("net:1.2.3.4", false)}, {}).isEmpty());
}

TEST(AutoConnect, EligibleIncluded) {
  auto r = devicesToConnect(true, {cand("usb:A", true)}, {});
  ASSERT_EQ(r.size(), 1); EXPECT_EQ(r[0], "usb:A");
}

TEST(AutoConnect, ActiveKeySkipped) {
  QSet<QString> active = {QString("usb:A")};
  EXPECT_TRUE(devicesToConnect(true, {cand("usb:A", true)}, active).isEmpty());
}

TEST(AutoConnect, MixedSelectsOnlyEligibleInactive) {
  QList<AutoConnectCandidate> cs = {
    cand("usb:A", true), cand("net:B", false), cand("net:C", true), cand("usb:D", true)};
  QSet<QString> active = {QString("usb:D")};
  auto r = devicesToConnect(true, cs, active);
  ASSERT_EQ(r.size(), 2); EXPECT_EQ(r[0], "usb:A"); EXPECT_EQ(r[1], "net:C");
}
