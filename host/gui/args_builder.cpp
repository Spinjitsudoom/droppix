#include "args_builder.h"

namespace droppix {

Command build_command(const Settings& s, const std::string& stream_bin,
                      int port, const std::string& touch_name,
                      const std::string& usb_aoa_serial) {
  const int use_port = (port >= 0) ? port : s.port;
  const std::string tname = touch_name.empty() ? "droppix-touch" : touch_name;
  std::vector<std::string> a;  // droppix_stream's own arguments
  if (s.source == Settings::Source::TestPattern) {
    a.push_back("--test-pattern");
  }
  // Both sources take the dimensions; evdi additionally advertises a refresh.
  a.push_back("--width");  a.push_back(std::to_string(s.width));
  a.push_back("--height"); a.push_back(std::to_string(s.height));
  if (s.source == Settings::Source::Evdi) {
    a.push_back("--refresh"); a.push_back(std::to_string(s.refresh_hz));
    if (s.touch) {
      a.push_back("--touch");   // touch injection (evdi/root only)
      a.push_back("--touch-name"); a.push_back(tname);
    }
    if (s.orientation != 0) {              // rotate the droppix output (evdi only)
      a.push_back("--orientation"); a.push_back(std::to_string(s.orientation));
    }
    a.push_back("--approve");  // host-side approval dialog gates non-localhost peers
  }
  a.push_back("--fps");     a.push_back(std::to_string(s.fps));
  a.push_back("--bitrate"); a.push_back(std::to_string(s.bitrate_kbps));
  a.push_back("--port");    a.push_back(std::to_string(use_port));
  a.push_back("--stats-json");
  if (s.audio) a.push_back("--audio");
  if (s.overlay) a.push_back("--overlay");
  if (!usb_aoa_serial.empty()) {
    // USB/AOA session: stream over the cable, no TLS (physical trust). The streamer
    // internally skips listen/TLS when --usb-aoa is set; --port is still harmless.
    a.push_back("--usb-aoa"); a.push_back(usb_aoa_serial);
  } else if (s.tls && !s.certPath.empty()) {
    a.push_back("--tls");
    a.push_back("--cert"); a.push_back(s.certPath);
    a.push_back("--key");  a.push_back(s.keyPath);
  }

  Command c;
  if (s.source == Settings::Source::Evdi) {
    c.program = "pkexec";
    c.args.push_back(stream_bin);                 // pkexec <binary> <args...>
    c.args.insert(c.args.end(), a.begin(), a.end());
  } else {
    c.program = stream_bin;
    c.args = a;
  }
  return c;
}
}  // namespace droppix
