#include "stats_parser.h"
#include <cstdlib>

namespace droppix {
namespace {
// Find "key": and parse the number/literal that follows. Returns false if absent.
bool find_value(const std::string& s, const std::string& key, std::string& out) {
  std::string needle = "\"" + key + "\":";
  auto p = s.find(needle);
  if (p == std::string::npos) return false;
  p += needle.size();
  auto end = s.find_first_of(",}", p);
  if (end == std::string::npos) return false;
  out = s.substr(p, end - p);
  return !out.empty();
}
bool get_double(const std::string& s, const std::string& key, double& out) {
  std::string v;
  if (!find_value(s, key, v)) return false;
  out = std::strtod(v.c_str(), nullptr);
  return true;
}
}  // namespace

Stats parse_stats_json(const std::string& line) {
  Stats st;
  std::string conn;
  if (!get_double(line, "encode_ms_avg", st.encode_ms_avg)) return st;
  if (!get_double(line, "encode_ms_peak", st.encode_ms_peak)) return st;
  if (!get_double(line, "fps", st.fps)) return st;
  if (!get_double(line, "frame_kb_avg", st.frame_kb_avg)) return st;
  if (!get_double(line, "frame_kb_peak", st.frame_kb_peak)) return st;
  if (!find_value(line, "client_connected", conn)) return st;
  st.client_connected = (conn == "true");
  st.valid = true;
  return st;
}
}  // namespace droppix
