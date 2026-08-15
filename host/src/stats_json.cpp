#include "stats_json.h"
#include <cstdio>

namespace droppix {

std::string format_stats_json(double encode_ms_avg, double encode_ms_peak,
                              double fps, double frame_kb_avg,
                              double frame_kb_peak, bool client_connected,
                              double interval_ms_avg,
                              double send_ms_avg, double send_ms_peak,
                              double backlog_kb_avg, double link_dropped_per_s) {
  char buf[512];
  std::snprintf(buf, sizeof(buf),
      "{\"encode_ms_avg\":%.1f,\"encode_ms_peak\":%.1f,\"fps\":%.1f,"
      "\"frame_kb_avg\":%.1f,\"frame_kb_peak\":%.1f,\"client_connected\":%s,"
      "\"interval_ms_avg\":%.1f,\"send_ms_avg\":%.1f,\"send_ms_peak\":%.1f,"
      "\"backlog_kb_avg\":%.1f,\"link_dropped_per_s\":%.1f}",
      encode_ms_avg, encode_ms_peak, fps, frame_kb_avg, frame_kb_peak,
      client_connected ? "true" : "false",
      interval_ms_avg, send_ms_avg, send_ms_peak, backlog_kb_avg, link_dropped_per_s);
  return std::string(buf);
}

}  // namespace droppix
