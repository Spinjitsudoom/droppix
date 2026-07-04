#include <gtest/gtest.h>
#include "mdns_browse.h"

using namespace droppix;

static const char* kSample =
  "+;eth0;IPv4;Nexus 10;_droppix-client._tcp;local\n"
  "=;eth0;IPv4;Nexus 10;_droppix-client._tcp;local;nexus.local;192.168.1.42;48000;\"\"\n";

TEST(MdnsBrowse, ParsesResolvedLine) {
  auto v = parse_avahi_browse(kSample);
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0].name, "Nexus 10");
  EXPECT_EQ(v[0].address, "192.168.1.42");
  EXPECT_EQ(v[0].port, 48000);
}

TEST(MdnsBrowse, IgnoresNonResolvedAndIPv6) {
  auto v = parse_avahi_browse("=;eth0;IPv6;X;_droppix-client._tcp;local;h;fe80::1;48000;\"\"\n");
  EXPECT_TRUE(v.empty());
}

TEST(MdnsBrowse, ParsesTxtId) {
  auto v = parse_avahi_browse(
    "=;eth0;IPv4;Nexus 10;_droppix-client._tcp;local;h;192.168.1.42;48000;\"id=dev-abc-123\"\n");
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0].id, "dev-abc-123");
}

TEST(MdnsBrowse, EmptyTxtGivesEmptyId) {
  auto v = parse_avahi_browse(
    "=;eth0;IPv4;Nexus 10;_droppix-client._tcp;local;h;192.168.1.42;48000;\"\"\n");
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0].id, "");
}

TEST(MdnsBrowse, IdRecordAnchoredNotSubstring) {
  auto v = parse_avahi_browse(
    "=;eth0;IPv4;Nexus 10;_droppix-client._tcp;local;h;192.168.1.42;48000;\"uuid=zzz\" \"id=real-abc\"\n");
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0].id, "real-abc");
}

TEST(MdnsBrowse, NoTxtFieldGivesEmptyId) {
  auto v = parse_avahi_browse(
    "=;eth0;IPv4;Nexus 10;_droppix-client._tcp;local;h;192.168.1.42;48000\n");
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0].id, "");
}
