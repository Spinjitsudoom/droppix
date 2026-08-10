#pragma once
#include <string>
namespace droppix {
// One-line flat JSON object for the GUI's --stats-json feed. The interval/send fields
// split low fps into its causes: interval_ms_avg = gap between compositor-delivered
// frames (capture-bound when high); send_ms_* = time blocked writing to the client
// socket (network-bound when high). The GUI parser looks fields up by key, so the
// extra keys are backward-compatible.
std::string format_stats_json(double encode_ms_avg, double encode_ms_peak,
                              double fps, double frame_kb_avg,
                              double frame_kb_peak, bool client_connected,
                              double interval_ms_avg = 0.0,
                              double send_ms_avg = 0.0, double send_ms_peak = 0.0);
}  // namespace droppix
