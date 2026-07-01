#include "usb_scan.h"

#include <sstream>

namespace droppix {

namespace {

// Trim trailing CR / whitespace (handles CRLF input).
std::string rstrip(std::string s) {
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n' ||
                        s.back() == ' '  || s.back() == '\t')) {
    s.pop_back();
  }
  return s;
}

}  // namespace

std::vector<AdbDevice> parse_adb_devices(const std::string& text) {
  std::vector<AdbDevice> devices;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    line = rstrip(line);
    // Only "<serial>\t<state>" lines have a tab; the header, "* daemon ..."
    // noise, and blank lines do not, so the tab check filters them out.
    auto tab = line.find('\t');
    if (tab == std::string::npos) continue;

    AdbDevice d;
    d.serial = line.substr(0, tab);
    std::istringstream rest(line.substr(tab + 1));
    rest >> d.state;  // first token only (ignores `-l` trailing info)
    if (d.serial.empty() || d.state.empty()) continue;
    devices.push_back(d);
  }
  return devices;
}

bool adb_package_present(const std::string& pm_output, const std::string& pkg) {
  static const std::string kPrefix = "package:";
  std::istringstream lines(pm_output);
  std::string line;
  while (std::getline(lines, line)) {
    line = rstrip(line);
    if (line.rfind(kPrefix, 0) != 0) continue;
    if (line.substr(kPrefix.size()) == pkg) return true;
  }
  return false;
}

}  // namespace droppix
