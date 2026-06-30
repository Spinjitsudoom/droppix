#pragma once
#include <csignal>
#include "frame_source.h"
#include "encoder.h"
#include "transport_server.h"
#include "input_map.h"   // droppix::Rect
#include "approval.h"    // droppix::ApprovalGate

namespace droppix {
struct StreamConfig {
  int fps = 30; int bitrate_kbps = 8000; bool stats_json = false;
  bool touch = false;          // enable uinput injection (off by default)
  Rect monitor{};              // droppix monitor rect (from --monitor); 0 => query kscreen
  int desktop_w = 0, desktop_h = 0;  // desktop bounds (from --desktop); 0 => query kscreen
  int orientation = 0;         // initial orientation code (0..3); only seeds dims now
  int* live_orientation = nullptr;  // session writes the latest reported code here so
                                    // the caller can rebuild the source at new dims
  bool approve = false;        // gate non-localhost peers on an approve/deny reply
  ApprovalGate* gate = nullptr;  // stdin-fed approval channel (owned by stream_main)
  bool audio = false;          // capture droppix-audio monitor and stream it
  bool overlay = false;        // tell the app to show its RTT/fps/decode overlay
};

class StreamDaemon {
 public:
  StreamDaemon(FrameSource& src, Encoder& enc, TransportServer& tx, StreamConfig cfg)
      : src_(src), enc_(enc), tx_(tx), cfg_(cfg) {}
  // Waits for a client + HELLO, opens the encoder at the source's dimensions,
  // sends CONFIG, then streams. Stops when `stop` is set or after `max_frames`
  // encoded frames (max_frames == 0 means unlimited). Returns true if it ran a
  // session (handshake completed).
  bool run_until(const volatile std::sig_atomic_t& stop, int max_frames);

 private:
  FrameSource& src_;
  Encoder& enc_;
  TransportServer& tx_;
  StreamConfig cfg_;
};
}  // namespace droppix
