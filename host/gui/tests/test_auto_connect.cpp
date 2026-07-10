#include "auto_connect.h"
#include <gtest/gtest.h>

using namespace droppix;

static AutoConnectCandidate cand(const QString& k, bool e, const QString& id = "") {
  AutoConnectCandidate c; c.key = k; c.id = id; c.eligible = e; return c;
}

TEST(AutoConnect, DisabledReturnsEmpty) {
  auto p = devicesToConnect(false, {cand("usb-aoa:A", true)}, {});
  EXPECT_TRUE(p.connect.isEmpty());
  EXPECT_TRUE(p.disconnect.isEmpty());
}

TEST(AutoConnect, IneligibleSkipped) {
  EXPECT_TRUE(devicesToConnect(true, {cand("net:1.2.3.4", false)}, {}).connect.isEmpty());
}

TEST(AutoConnect, EligibleIncluded) {
  auto p = devicesToConnect(true, {cand("usb-aoa:A", true)}, {});
  ASSERT_EQ(p.connect.size(), 1); EXPECT_EQ(p.connect[0], "usb-aoa:A");
}

TEST(AutoConnect, ActiveKeySkipped) {
  auto p = devicesToConnect(true, {cand("usb-aoa:A", true)}, {{"usb-aoa:A", ""}});
  EXPECT_TRUE(p.connect.isEmpty());
}

TEST(AutoConnect, MixedSelectsOnlyEligibleInactive) {
  QList<AutoConnectCandidate> cs = {
    cand("usb-aoa:A", true), cand("net:B", false), cand("net:C", true), cand("usb-aoa:D", true)};
  auto p = devicesToConnect(true, cs, {{"usb-aoa:D", ""}});
  ASSERT_EQ(p.connect.size(), 2);
  EXPECT_EQ(p.connect[0], "usb-aoa:A"); EXPECT_EQ(p.connect[1], "net:C");
}

TEST(AutoConnect, ExcludesCandidateWhoseIdIsActive) {
  auto p = devicesToConnect(true, {cand("net:1.2.3.4", true, "dev-x")},
                            {{"usb-aoa:S", "dev-x"}});
  EXPECT_TRUE(p.connect.isEmpty());   // same tablet already connected over the cable
  EXPECT_TRUE(p.disconnect.isEmpty());
}

TEST(AutoConnect, EmptyIdNotDedupedById) {
  auto p = devicesToConnect(true, {cand("net:a", true, "")}, {{"net:z", ""}});
  ASSERT_EQ(p.connect.size(), 1); EXPECT_EQ(p.connect[0], "net:a");
}

TEST(AutoConnect, UsbPreferredOverNetForSameIdRegardlessOfOrder) {
  // Both transports discovered for the same tablet in one pass (net listed first):
  // only the cable connects.
  QList<AutoConnectCandidate> cs = {
    cand("net:1.2.3.4", true, "dev-x"), cand("usb-aoa:S", true, "dev-x")};
  auto p = devicesToConnect(true, cs, {});
  ASSERT_EQ(p.connect.size(), 1);
  EXPECT_EQ(p.connect[0], "usb-aoa:S");
  EXPECT_TRUE(p.disconnect.isEmpty());
}

TEST(AutoConnect, UsbTakesOverActiveNetSession) {
  // Tablet streaming over Wi-Fi; its cable becomes usable: stop the net session now,
  // connect nothing this pass (the follow-up evaluation starts USB).
  auto p = devicesToConnect(true, {cand("usb-aoa:S", true, "dev-x")},
                            {{"net:1.2.3.4", "dev-x"}});
  EXPECT_TRUE(p.connect.isEmpty());
  ASSERT_EQ(p.disconnect.size(), 1);
  EXPECT_EQ(p.disconnect[0], "net:1.2.3.4");
}

TEST(AutoConnect, IneligibleUsbDoesNotDisturbActiveNetSession) {
  // First-ever cable use (not in the known-AOA store) must not kill the Wi-Fi session.
  auto p = devicesToConnect(true, {cand("usb-aoa:S", false, "dev-x")},
                            {{"net:1.2.3.4", "dev-x"}});
  EXPECT_TRUE(p.connect.isEmpty());
  EXPECT_TRUE(p.disconnect.isEmpty());
}

TEST(AutoConnect, NetCandidateStillConnectsWhenNoUsbRival) {
  auto p = devicesToConnect(true, {cand("net:1.2.3.4", true, "dev-x")}, {});
  ASSERT_EQ(p.connect.size(), 1); EXPECT_EQ(p.connect[0], "net:1.2.3.4");
}
