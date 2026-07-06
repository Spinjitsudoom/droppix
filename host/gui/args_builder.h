#pragma once
#include <string>
#include <vector>
#include "settings.h"
namespace droppix {
struct Command {
  std::string program;             // droppix_stream path, or "pkexec"
  std::vector<std::string> args;   // arguments (incl. the binary path if pkexec)
};
// port < 0 uses s.port; empty touch_name uses "droppix-touch". Multi-monitor passes a
// per-session port + a unique touch device name (droppix-touch-<port>). A non-empty
// usb_aoa_serial makes this a USB/AOA session: adds --usb-aoa <serial> and omits TLS
// (the cable is the trust boundary); each concurrent monitor targets its own serial.
Command build_command(const Settings& s, const std::string& stream_bin,
                      int port = -1, const std::string& touch_name = "",
                      const std::string& usb_aoa_serial = "");
}  // namespace droppix
