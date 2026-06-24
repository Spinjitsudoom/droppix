#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/prctl.h>
#include "stream_daemon.h"
#include "test_pattern_source.h"
#include "evdi_frame_source.h"
#include "software_encoder.h"

static volatile std::sig_atomic_t g_stop = 0;
static void on_sigint(int) { g_stop = 1; }

int main(int argc, char** argv) {
  std::signal(SIGINT, on_sigint);
  std::signal(SIGTERM, on_sigint);          // GUI terminate() -> clean shutdown
  prctl(PR_SET_PDEATHSIG, SIGTERM);          // die if our parent (e.g. pkexec) is killed
  int port = 27000, fps = 30, bitrate = 8000, frames = 0;
  int width = 1280, height = 720;
  bool test_pattern = false, adb_reverse = false, stats_json = false;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto val = [&]() { return (i + 1 < argc) ? std::atoi(argv[++i]) : 0; };
    if (a == "--test-pattern") test_pattern = true;
    else if (a == "--adb-reverse") adb_reverse = true;
    else if (a == "--stats-json") stats_json = true;
    else if (a == "--port") port = val();
    else if (a == "--fps") fps = val();
    else if (a == "--bitrate") bitrate = val();
    else if (a == "--width") width = val();
    else if (a == "--height") height = val();
    else if (a == "--frames") frames = val();
    else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
  }

  if (fps <= 0) fps = 30;
  if (bitrate <= 0) bitrate = 8000;
  if (width <= 0) width = 1280;
  if (height <= 0) height = 720;

  droppix::TransportServer tx;
  if (!tx.listen(static_cast<uint16_t>(port))) {
    std::fprintf(stderr, "listen on %d failed\n", port); return 1;
  }
  std::fprintf(stderr, "listening on port %d\n", tx.port());

  if (adb_reverse) {
    std::string cmd = "adb reverse tcp:" + std::to_string(port) +
                      " tcp:" + std::to_string(port);
    std::fprintf(stderr, "running: %s\n", cmd.c_str());
    if (std::system(cmd.c_str()) != 0)
      std::fprintf(stderr, "warning: adb reverse failed\n");
  }

  // Reconnect loop: keep serving sessions until SIGINT. One-shot when --frames>0.
  while (!g_stop) {
    droppix::SoftwareEncoder enc;
    droppix::TestPatternSource pattern(width, height, fps);
    droppix::EvdiFrameSource evdi;
    droppix::FrameSource& src =
        test_pattern ? static_cast<droppix::FrameSource&>(pattern)
                     : static_cast<droppix::FrameSource&>(evdi);
    droppix::StreamDaemon daemon(src, enc, tx, {fps, bitrate, stats_json});
    daemon.run_until(g_stop, frames);
    if (frames > 0) break;  // one-shot (test) mode exits after a single session
  }
  return 0;
}
