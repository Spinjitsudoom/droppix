#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>
#include <sys/prctl.h>
#include "stream_daemon.h"
#include "test_pattern_source.h"
#include "evdi_frame_source.h"
#include "approval.h"
#include "aoa_connect.h"
#include "encoder_factory.h"

static volatile std::sig_atomic_t g_stop = 0;
static void on_sigint(int) { g_stop = 1; }

// Approve/deny replies for non-localhost peers arrive as lines on stdin (see the
// stdin reader thread below); the daemon's run_until() blocks on g_gate.wait(id).
static droppix::ApprovalGate g_gate;

// Latest orientation code (0..3) the tablet reported; seeds each session's dims.
// Written by the daemon's orientation handler, read here to rebuild at new dims.
// Single-threaded (handler runs in the daemon loop; we read between sessions).
static int g_orientation = 0;

// Host-side perf-overlay toggle (0/1). The GUI sends "overlay N" lines on stdin
// (see the reader thread); the daemon loop reads this and pushes OVERLAY to the
// app on change. Cross-thread (stdin thread writes, daemon loop reads) -> atomic.
static std::atomic<int> g_overlay{0};

int main(int argc, char** argv) {
  std::signal(SIGINT, on_sigint);
  std::signal(SIGTERM, on_sigint);          // GUI terminate() -> clean shutdown
  prctl(PR_SET_PDEATHSIG, SIGTERM);          // die if our parent (e.g. pkexec) is killed
  int port = 27000, fps = 30, bitrate = 8000, frames = 0;
  int width = 1920, height = 1080, refresh = 60;
  bool test_pattern = false, stats_json = false, touch = false, mirror = false;
  std::string touch_name = "droppix-touch";   // uinput device name (unique per session for multi-monitor)
  bool approve = false;
  bool audio = false;
  bool overlay = false;
  bool tls = false;
  std::string cert, key;
  std::string usb_aoa;   // --usb-aoa <serial>: serve one tablet over USB (AOA), not TCP
  int mx = 0, my = 0, mw = 0, mh = 0, dtw = 0, dth = 0;  // --monitor / --desktop
  int orientation = 0;                                   // --orientation 0/90/180/270
  droppix::EncoderPref encoder_pref = droppix::EncoderPref::Auto;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto val = [&]() { return (i + 1 < argc) ? std::atoi(argv[++i]) : 0; };
    auto sval = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };
    if (a == "--test-pattern") test_pattern = true;
    else if (a == "--stats-json") stats_json = true;
    else if (a == "--touch") touch = true;
    else if (a == "--mirror") mirror = true;
    else if (a == "--touch-name") touch_name = sval();
    else if (a == "--approve") approve = true;
    else if (a == "--port") port = val();
    else if (a == "--fps") fps = val();
    else if (a == "--bitrate") bitrate = val();
    else if (a == "--width") width = val();
    else if (a == "--height") height = val();
    else if (a == "--refresh") refresh = val();
    else if (a == "--frames") frames = val();
    else if (a == "--monitor") { std::sscanf(sval(), "%d,%d,%d,%d", &mx, &my, &mw, &mh); }
    else if (a == "--desktop") { std::sscanf(sval(), "%dx%d", &dtw, &dth); }
    else if (a == "--orientation") orientation = val();
    else if (a == "--tls") tls = true;
    else if (a == "--cert") cert = sval();
    else if (a == "--key") key = sval();
    else if (a == "--audio") audio = true;
    else if (a == "--overlay") overlay = true;
    else if (a == "--usb-aoa") usb_aoa = sval();
    else if (a == "--encoder") encoder_pref = droppix::parse_encoder_pref(sval());
    else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
  }

  if (fps <= 0) fps = 30;
  if (bitrate <= 0) bitrate = 8000;
  if (width <= 0) width = 1920;
  if (height <= 0) height = 1080;
  if (refresh <= 0) refresh = 60;
  if (orientation != 90 && orientation != 180 && orientation != 270) orientation = 0;
  g_orientation = orientation / 90;   // initial code: 0/90/180/270 -> 0/1/2/3
  g_overlay.store(overlay ? 1 : 0);   // seed the live toggle from the start-up flag

  droppix::TransportServer tx;
  // AOA (--usb-aoa) serves one tablet over the USB cable, not TCP: no listen, no TLS (the
  // physical cable is the trust boundary). Otherwise, listen on the TCP port as usual.
  if (usb_aoa.empty()) {
    if (!tx.listen(static_cast<uint16_t>(port))) {
      std::fprintf(stderr, "listen on %d failed\n", port); return 1;
    }
    std::fprintf(stderr, "listening on port %d\n", tx.port());
    if (tls) tx.enable_tls(cert, key);
  } else {
    std::fprintf(stderr, "usb-aoa mode: serial=%s\n", usb_aoa.c_str());
  }

  // Stop channel for the GUI: it launches us via pkexec as ROOT, so it cannot signal
  // us (terminate/kill -> EPERM) — PR_SET_PDEATHSIG only fires if the GUI itself exits.
  // Instead the GUI closes our stdin on Stop; watch for that EOF and shut down. Only
  // when stdin is a pipe (the GUI), not a tty (CLI uses Ctrl-C/SIGINT) and not the
  // one-shot --frames test mode.
  if (frames == 0 && !isatty(STDIN_FILENO)) {
    std::thread([]{
      char c;
      std::string buf;
      while (::read(STDIN_FILENO, &c, sizeof(c)) > 0) {
        if (c == '\n') {
          std::string id; bool allow = false;
          if (buf.rfind("overlay ", 0) == 0)            // "overlay 0" / "overlay 1": host-side toggle
            g_overlay.store(buf.size() > 8 && buf[8] == '1' ? 1 : 0);
          else if (droppix::parse_approval(buf, id, allow)) g_gate.submit(id, allow);
          buf.clear();
        } else {
          buf.push_back(c);
        }
      }
      g_stop = 1;   // EOF/error on stdin -> ask for a graceful shutdown
      // If we don't exit promptly (e.g. blocked waiting for a client between sessions),
      // force it — the kernel closes the evdi fd, which tears down the virtual monitor.
      std::this_thread::sleep_for(std::chrono::milliseconds(1500));
      std::_Exit(0);
    }).detach();
  }

  // The daemon builds the source AFTER the tablet's HELLO, sized to the tablet's native
  // resolution (or --width/--height when the client reports 0). On a portrait<->landscape
  // rotation the daemon ends the session; the reconnect's HELLO gives the swapped dims.
  auto make_source = [&](int w, int h) -> std::unique_ptr<droppix::FrameSource> {
    if (w <= 0) w = width;
    if (h <= 0) h = height;
    if (test_pattern) return std::make_unique<droppix::TestPatternSource>(w, h, fps);
    // Use the session's port as the EDID serial so each concurrent droppix monitor has a
    // distinct EDID identity (identical serials make KWin dedupe them: only one output
    // appears in Display settings, the rest stay frozen). Ports are unique per session.
    return std::make_unique<droppix::EvdiFrameSource>(w, h, refresh,
                                                      static_cast<uint32_t>(port));
  };

  // Reconnect loop: keep serving sessions until SIGINT. One-shot when --frames>0.
  while (!g_stop) {
    // AOA: run the accessory handshake and adopt the USB channel before streaming; retry
    // until the tablet is plugged in + the app opens the accessory.
    if (!usb_aoa.empty()) {
      auto ch = droppix::aoa_connect(usb_aoa);
      if (!ch) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        continue;
      }
      tx.adopt_channel(std::move(ch));
    }
    auto enc = droppix::make_encoder(encoder_pref);
    droppix::StreamDaemon daemon(make_source, *enc, tx,
        {fps, bitrate, stats_json, touch, mirror, touch_name, droppix::Rect{mx, my, mw, mh}, dtw, dth,
         orientation, &g_orientation, approve, &g_gate, audio, overlay, &g_overlay,
         /*preconnected=*/!usb_aoa.empty()});
    daemon.run_until(g_stop, frames);
    if (frames > 0) break;  // one-shot (test) mode exits after a single session
  }
  return 0;
}
