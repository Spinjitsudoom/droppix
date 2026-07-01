#include <gtest/gtest.h>
#include "usb_scan.h"

using namespace droppix;

TEST(UsbScan, EmptyAndHeaderOnlyYieldNoDevices) {
  EXPECT_TRUE(parse_adb_devices("").empty());
  EXPECT_TRUE(parse_adb_devices("List of devices attached\n").empty());
  EXPECT_TRUE(parse_adb_devices("List of devices attached\n\n").empty());
}

TEST(UsbScan, ParsesSingleDevice) {
  auto d = parse_adb_devices("List of devices attached\nR32D204ZH6J\tdevice\n");
  ASSERT_EQ(d.size(), 1u);
  EXPECT_EQ(d[0].serial, "R32D204ZH6J");
  EXPECT_EQ(d[0].state, "device");
}

TEST(UsbScan, ParsesMultipleDevicesAndStates) {
  auto d = parse_adb_devices(
      "List of devices attached\n"
      "AAAA\tdevice\n"
      "BBBB\tunauthorized\n"
      "CCCC\toffline\n");
  ASSERT_EQ(d.size(), 3u);
  EXPECT_EQ(d[0].serial, "AAAA"); EXPECT_EQ(d[0].state, "device");
  EXPECT_EQ(d[1].serial, "BBBB"); EXPECT_EQ(d[1].state, "unauthorized");
  EXPECT_EQ(d[2].serial, "CCCC"); EXPECT_EQ(d[2].state, "offline");
}

TEST(UsbScan, SkipsDaemonNoiseAndToleratesCrlf) {
  auto d = parse_adb_devices(
      "* daemon not running; starting now at tcp:5037\r\n"
      "* daemon started successfully\r\n"
      "List of devices attached\r\n"
      "R32D204ZH6J\tdevice\r\n");
  ASSERT_EQ(d.size(), 1u);
  EXPECT_EQ(d[0].serial, "R32D204ZH6J");
  EXPECT_EQ(d[0].state, "device");
}

TEST(UsbScan, StateIsFirstTokenIgnoringTrailingInfo) {
  // `adb devices -l` appends product/model/device after the state.
  auto d = parse_adb_devices(
      "List of devices attached\n"
      "R32D204ZH6J\tdevice product:mantaray model:Nexus_10 device:manta\n");
  ASSERT_EQ(d.size(), 1u);
  EXPECT_EQ(d[0].serial, "R32D204ZH6J");
  EXPECT_EQ(d[0].state, "device");
}

TEST(UsbScan, PackagePresentRequiresExactMatch) {
  const std::string out = "package:com.android.foo\npackage:com.droppix.app\n";
  EXPECT_TRUE(adb_package_present(out, "com.droppix.app"));
  EXPECT_FALSE(adb_package_present(out, "com.droppix"));                    // prefix, not a match
  EXPECT_FALSE(adb_package_present("package:com.droppix.app2\n", "com.droppix.app"));
  EXPECT_FALSE(adb_package_present("", "com.droppix.app"));
}
