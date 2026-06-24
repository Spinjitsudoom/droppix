#pragma once
#include <string>
namespace droppix {
struct Stats {
  double encode_ms_avg = 0, encode_ms_peak = 0, fps = 0;
  double frame_kb_avg = 0, frame_kb_peak = 0;
  bool client_connected = false;
  bool valid = false;
};
// Parse one flat --stats-json line. Missing required key -> valid=false.
Stats parse_stats_json(const std::string& line);
}  // namespace droppix
