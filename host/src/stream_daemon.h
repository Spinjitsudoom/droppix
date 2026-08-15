#pragma once
#include <atomic>
#include <csignal>
#include <functional>
#include <memory>
#include <string>
#include "frame_source.h"
#include "encoder.h"
#include "transport_server.h"
#include "input_map.h"   // droppix::Rect
#include "approval.h"    // droppix::ApprovalGate
#include "desktop_backend.h"  // droppix::DesktopBackend, make_desktop_backend

namespace droppix {
struct StreamConfig {
  int fps = 30; int bitrate_kbps = 8000; bool stats_json = false;
  bool touch = false;          // enable uinput injection (off by default)
  bool mirror = false;         // mirror mode: evdi at the primary's resolution + apply_layout(Mirror)
  std::string touch_name = "droppix-touch";  // uinput device name (unique per session for multi-monitor)
  Rect monitor{};              // droppix monitor rect (from --monitor); 0 => query kscreen
  int desktop_w = 0, desktop_h = 0;  // desktop bounds (from --desktop); 0 => query kscreen
  int orientation = 0;         // initial orientation code (0..3); only seeds dims now
  int* live_orientation = nullptr;  // session writes the latest reported code here so
                                    // the caller can rebuild the source at new dims
  bool approve = false;        // gate non-localhost peers on an approve/deny reply
  ApprovalGate* gate = nullptr;  // stdin-fed approval channel (owned by stream_main)
  bool audio = false;          // capture droppix-audio monitor and stream it
  bool overlay = false;        // initial: tell the app to show its RTT/fps/decode overlay
  std::atomic<int>* live_overlay = nullptr;  // host-side toggle (GUI "overlay N" on stdin);
                                             // the loop pushes OVERLAY when this changes
  bool preconnected = false;   // AOA: caller already adopted a ByteChannel; skip accept_client
};

class StreamDaemon {
 public:
  // make_source(w, h, fps) builds the frame source at the requested dimensions — called
  // AFTER the client's HELLO so an evdi monitor is sized to the tablet's native resolution.
  // The factory may substitute defaults when w/h are 0 (client didn't report).
  //
  // `fps` is the NEGOTIATED frame rate (the client's HELLO wins over the host default), so
  // an evdi source can advertise a mode fast enough to deliver it. Without it the virtual
  // monitor's refresh came only from --refresh: a client asking 60 fps against a 30 Hz mode
  // was silently capped at 30, with nothing in the logs to say why.
  StreamDaemon(std::function<std::unique_ptr<FrameSource>(int, int, int)> make_source,
               Encoder& enc, TransportServer& tx, StreamConfig cfg)
      : make_source_(std::move(make_source)), enc_(enc), tx_(tx), cfg_(cfg) {}
  // Waits for a client + HELLO, builds the source at the reported size, opens the encoder,
  // sends CONFIG, then streams. Stops when `stop` is set or after `max_frames` encoded frames
  // (0 = unlimited). Returns true if it ran a session (handshake completed).
  bool run_until(const volatile std::sig_atomic_t& stop, int max_frames);

 private:
  std::function<std::unique_ptr<FrameSource>(int, int, int)> make_source_;
  std::unique_ptr<FrameSource> src_;
  Encoder& enc_;
  TransportServer& tx_;
  StreamConfig cfg_;
  std::shared_ptr<DesktopBackend> desktop_ = make_desktop_backend();
};
}  // namespace droppix
