#include "lan_ifaces.h"
#include <gtest/gtest.h>

using namespace droppix;

static QList<LanIface> sample() {
  return {
      {"192.168.1.11", "Wi-Fi"},
      {"192.168.224.1", "VMware VMnet8"},
      {"192.168.80.1", "VMware VMnet1"},
  };
}

TEST(InterfaceFilter, EmptyExcludedReturnsAllInOrder) {
  const auto got = included_ifaces(sample(), {});
  ASSERT_EQ(got.size(), 3);
  EXPECT_EQ(got[0].ip, QStringLiteral("192.168.1.11"));
  EXPECT_EQ(got[2].name, QStringLiteral("VMware VMnet1"));
}

TEST(InterfaceFilter, ExcludeVmnetLeavesRealAdapter) {
  const QSet<QString> excluded{"VMware VMnet8", "VMware VMnet1"};
  const auto got = included_ifaces(sample(), excluded);
  ASSERT_EQ(got.size(), 1);
  EXPECT_EQ(got[0].ip, QStringLiteral("192.168.1.11"));
}

TEST(InterfaceFilter, ExcludeAllReturnsEmpty) {
  const QSet<QString> excluded{"Wi-Fi", "VMware VMnet8", "VMware VMnet1"};
  EXPECT_TRUE(included_ifaces(sample(), excluded).isEmpty());
}

TEST(InterfaceFilter, ExcludeUnknownNameIsNoop) {
  const auto got = included_ifaces(sample(), {"nonexistent0"});
  EXPECT_EQ(got.size(), 3);
}
