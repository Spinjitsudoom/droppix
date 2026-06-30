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
#include "software_encoder.h"
#include "approval.h"

static volatile std::sig_atomic_t g_stop = 0;
static void on_sigint(int) { g_stop = 1; }

// Approve/deny replies for non-localhost peers arrive as lines on stdin (see the
// stdin reader thread below); the daemon's run_until() blocks on g_gate.wait(id).
static droppix::ApprovalGate g_gate;

// Latest orientation code (0..3) the tablet reported; seeds each session's dims.
// Written by the daemon's orientation handler, read here to rebuild at new dims.
// Single-threaded (handler runs in the daemon loop; we read between sessions).
static int g_orientation = 0;

int main(int argc, char** argv) {
  std::signal(SIGINT, on_sigint);
  std::signal(SIGTERM, on_sigint);          // GUI terminate() -> clean shutdown
  prctl(PR_SET_PDEATHSIG, SIGTERM);          // die if our parent (e.g. pkexec) is killed
  int port = 27000, fps = 30, bitrate = 8000, frames = 0;
  int width = 1920, height = 1080, refresh = 60;
  bool test_pattern = false, adb_reverse = false, stats_json = false, touch = false;
  bool approve = false;
  bool audio = false;
  bool overlay = false;
  bool tls = false;
  std::string cert, key;
  int mx = 0, my = 0, mw = 0, mh = 0, dtw = 0, dth = 0;  // --monitor / --desktop
  int orientation = 0;                                   // --orientation 0/90/180/270

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto val = [&]() { return (i + 1 < argc) ? std::atoi(argv[++i]) : 0; };
    auto sval = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };
    if (a == "--test-pattern") test_pattern = true;
    else if (a == "--adb-reverse") adb_reverse = true;
    else if (a == "--stats-json") stats_json = true;
    else if (a == "--touch") touch = true;
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
    else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
  }

  if (fps <= 0) fps = 30;
  if (bitrate <= 0) bitrate = 8000;
  if (width <= 0) width = 1920;
  if (height <= 0) height = 1080;
  if (refresh <= 0) refresh = 60;
  if (orientation != 90 && orientation != 180 && orientation != 270) orientation = 0;
  g_orientation = orientation / 90;   // initial code: 0/90/180/270 -> 0/1/2/3

  droppix::TransportServer tx;
  if (!tx.listen(static_cast<uint16_t>(port))) {
    std::fprintf(stderr, "listen on %d failed\n", port); return 1;
  }
  std::fprintf(stderr, "listening on port %d\n", tx.port());

  if (tls) tx.enable_tls(cert, key);

  if (adb_reverse) {
    std::string cmd = "adb reverse tcp:" + std::to_string(port) +
                      " tcp:" + std::to_string(port);
    std::fprintf(stderr, "running: %s\n", cmd.c_str());
    if (std::system(cmd.c_str()) != 0)
      std::fprintf(stderr, "warning: adb reverse failed\n");
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
          if (droppix::parse_approval(buf, id, allow)) g_gate.submit(id, allow);
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

  // Reconnect loop: keep serving sessions until SIGINT. One-shot when --frames>0.
  // The stream is portrait- or landscape-SHAPED per the tablet's reported orientation;
  // when it crosses that boundary the daemon ends the session and we rebuild here at
  // the swapped dimensions (the app reconnects).
  while (!g_stop) {
    bool portrait = (g_orientation == 1 || g_orientation == 3);
    int sw = portrait ? height : width;
    int sh = portrait ? width : height;
    droppix::SoftwareEncoder enc;
    droppix::TestPatternSource pattern(sw, sh, fps);
    droppix::EvdiFrameSource evdi(sw, sh, refresh);
    droppix::FrameSource& src =
        test_pattern ? static_cast<droppix::FrameSource&>(pattern)
                     : static_cast<droppix::FrameSource&>(evdi);
    droppix::StreamDaemon daemon(src, enc, tx,
        {fps, bitrate, stats_json, touch, droppix::Rect{mx, my, mw, mh}, dtw, dth,
         orientation, &g_orientation, approve, &g_gate, audio, overlay});
    daemon.run_until(g_stop, frames);
    if (frames > 0) break;  // one-shot (test) mode exits after a single session
  }
  return 0;
}
