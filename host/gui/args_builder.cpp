#include "args_builder.h"

namespace droppix {

Command build_command(const Settings& s, const std::string& stream_bin) {
  std::vector<std::string> a;  // droppix_stream's own arguments
  if (s.source == Settings::Source::TestPattern) {
    a.push_back("--test-pattern");
  }
  // Both sources take the dimensions; evdi additionally advertises a refresh.
  a.push_back("--width");  a.push_back(std::to_string(s.width));
  a.push_back("--height"); a.push_back(std::to_string(s.height));
  if (s.source == Settings::Source::Evdi) {
    a.push_back("--refresh"); a.push_back(std::to_string(s.refresh_hz));
  }
  a.push_back("--fps");     a.push_back(std::to_string(s.fps));
  a.push_back("--bitrate"); a.push_back(std::to_string(s.bitrate_kbps));
  a.push_back("--port");    a.push_back(std::to_string(s.port));
  a.push_back("--stats-json");

  Command c;
  c.needs_adb_reverse = s.auto_adb_reverse;
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
