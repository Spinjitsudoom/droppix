#pragma once
#include <string>
#include <vector>

namespace droppix {

// A device reported by `adb devices`. `state` is the raw second column:
// "device" (ready), "unauthorized" (RSA prompt pending on the tablet),
// "offline", etc.
struct AdbDevice {
  std::string serial;
  std::string state;
};

// Parses plain `adb devices` output. Each device line is "<serial>\t<state>".
// Lines without a tab (the "List of devices attached" header, "* daemon ..."
// startup noise, blank lines) are skipped. Trailing CR (CRLF) is tolerated, and
// `state` is the first whitespace-delimited token after the tab (so `adb devices
// -l` trailing product/model info is ignored).
std::vector<AdbDevice> parse_adb_devices(const std::string& text);

// True if `pm list packages` output contains a line "package:<pkg>" matching the
// package name EXACTLY (so "com.droppix.app" does not match "com.droppix.app2"
// nor the prefix "com.droppix").
bool adb_package_present(const std::string& pm_output, const std::string& pkg);

}  // namespace droppix
