#pragma once
#include <string>
namespace droppix {
struct Settings {
  enum class Source { TestPattern, Evdi };
  Source source = Source::TestPattern;
  int width = 1280, height = 720;
  int fps = 30, bitrate_kbps = 8000, port = 27000;
  int refresh_hz = 60;
  bool auto_adb_reverse = true;
  bool touch = false;   // enable tablet touch -> cursor (evdi only)
  bool audio = false;   // capture droppix-audio sink and stream it to the tablet
  bool overlay = false; // tell the tablet to show its RTT/fps/decode overlay
  int orientation = 0;  // droppix output rotation degrees: 0/90/180/270 (evdi only)
  bool tls = true;          // pass --tls/--cert/--key to the streamer
  std::string certPath;     // PC's TLS cert (PEM)
  std::string keyPath;      // PC's TLS key (PEM)
};
}  // namespace droppix
