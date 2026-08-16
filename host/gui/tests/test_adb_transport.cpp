#include <gtest/gtest.h>

#include "adb_transport.h"

using namespace droppix;

// The Android client's "Connect via USB" button dials 127.0.0.1:<port>, which only reaches
// the PC through `adb reverse`. Picking the wrong set of serials here is the difference
// between that button working and it silently connecting to nothing.

TEST(AdbTransport, TakesReadyDevices) {
  const QString out =
      "List of devices attached\n"
      "d44407270407\tdevice\n";
  const auto ready = parse_ready_serials(out);
  EXPECT_EQ(ready.size(), 1);
  EXPECT_TRUE(ready.contains("d44407270407"));
}

// A phone sitting on the "Allow USB debugging?" dialog is attached but cannot carry a
// tunnel. Accepting it would mark it done and never retry once the user tapped Allow.
TEST(AdbTransport, SkipsUnauthorizedAndOffline) {
  const QString out =
      "List of devices attached\n"
      "AAAA\tunauthorized\n"
      "BBBB\toffline\n"
      "CCCC\tdevice\n";
  const auto ready = parse_ready_serials(out);
  EXPECT_EQ(ready.size(), 1);
  EXPECT_TRUE(ready.contains("CCCC"));
  EXPECT_FALSE(ready.contains("AAAA"));
  EXPECT_FALSE(ready.contains("BBBB"));
}

// A cold `adb devices` starts the daemon first and prints its chatter above the list.
TEST(AdbTransport, IgnoresDaemonChatterAndHeader) {
  const QString out =
      "* daemon not running; starting now at tcp:5037\n"
      "* daemon started successfully\n"
      "List of devices attached\n"
      "ZZZZ\tdevice\n";
  const auto ready = parse_ready_serials(out);
  EXPECT_EQ(ready.size(), 1);
  EXPECT_TRUE(ready.contains("ZZZZ"));
}

TEST(AdbTransport, NoDevicesIsEmptyNotAPhantom) {
  EXPECT_TRUE(parse_ready_serials("List of devices attached\n\n").isEmpty());
  EXPECT_TRUE(parse_ready_serials("").isEmpty());
}

// Serials must survive verbatim — a mangled one addresses no device.
TEST(AdbTransport, KeepsSerialExact) {
  const auto ready = parse_ready_serials("List of devices attached\nemulator-5554\tdevice\n");
  EXPECT_TRUE(ready.contains("emulator-5554"));
}

TEST(AdbTransport, HandlesMultipleReadyDevices) {
  const QString out =
      "List of devices attached\n"
      "AAAA\tdevice\n"
      "BBBB\tdevice\n";
  const auto ready = parse_ready_serials(out);
  EXPECT_EQ(ready.size(), 2);
}
