#pragma once
#include <string>
namespace droppix {
// One-line flat JSON object for the GUI's --stats-json feed.
std::string format_stats_json(double encode_ms_avg, double encode_ms_peak,
                              double fps, double frame_kb_avg,
                              double frame_kb_peak, bool client_connected);
}  // namespace droppix
