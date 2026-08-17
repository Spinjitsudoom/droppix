#pragma once
#include <string>
namespace droppix {
struct Settings {
  enum class Source { TestPattern, Evdi };
  Source source = Source::TestPattern;
  int width = 1280, height = 720;
  int fps = 30, bitrate_kbps = 8000, port = 27000;
  int refresh_hz = 60;
  bool auto_adb_reverse = true;   // always on; no GUI toggle (set true in SettingsPage::store)
  bool touch = false;   // enable tablet touch -> cursor (evdi only)
  bool audio = false;   // capture droppix-audio sink and stream it to the tablet
  bool overlay = false; // tell the tablet to show its RTT/fps/decode overlay
  bool autoConnect = true;  // auto-connect known tablets (USB + paired Wi-Fi) on discovery
  int orientation = 0;  // droppix output rotation degrees: 0/90/180/270 (evdi only)
  bool tls = true;          // pass --tls/--cert/--key to the streamer
  std::string certPath;     // PC's TLS cert (PEM), signed by caPath
  std::string keyPath;      // PC's TLS key (PEM)
  std::string caPath;       // local CA public cert (PEM) that signed certPath; served at /ca.crt
  bool webClient = false;   // serve host PWA + WSS on the session port (--web)
  std::string webRoot;      // resolved at collect-time; not persisted
  // File the GUI keeps the web client's settings in, so they survive a browser that will
  // not (origin-scoped localStorage, cleared for click-through certs). Resolved at
  // collect-time; not persisted as a setting itself.
  std::string clientSettingsPath;
};
}  // namespace droppix
