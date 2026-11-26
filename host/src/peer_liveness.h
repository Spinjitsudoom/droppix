#pragma once
#include <cstdint>

namespace droppix {

// Has a session's client gone quiet? Clients that PING (the Android app every second, the web
// client) are declared gone after `limit_ms` without ANY message. Needed because some channels
// can't see the other end leave: when the tablet app closes its end of the USB accessory, the
// PC's reads just time out, and a static screen sends nothing that could fail. Armed only once a
// PING has arrived, so a client that never pings (the Linux desktop client) is never cut off.
class PeerLiveness {
 public:
  void reset() { armed_ = false; last_ms_ = 0; }
  void heard(int64_t now_ms, bool ping) {
    last_ms_ = now_ms;
    if (ping) armed_ = true;
  }
  bool silent(int64_t now_ms, int64_t limit_ms) const {
    return armed_ && now_ms - last_ms_ >= limit_ms;
  }

 private:
  bool armed_ = false;
  int64_t last_ms_ = 0;
};

}  // namespace droppix
