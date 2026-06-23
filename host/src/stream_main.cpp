#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "stream_daemon.h"
#include "test_pattern_source.h"
#include "software_encoder.h"

static volatile std::sig_atomic_t g_stop = 0;
static void on_sigint(int) { g_stop = 1; }

int main(int argc, char** argv) {
  std::signal(SIGINT, on_sigint);
  int port = 27000, fps = 30, bitrate = 8000, frames = 0;
  int width = 1280, height = 720;
  bool test_pattern = false, adb_reverse = false;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto val = [&]() { return (i + 1 < argc) ? std::atoi(argv[++i]) : 0; };
    if (a == "--test-pattern") test_pattern = true;
    else if (a == "--adb-reverse") adb_reverse = true;
    else if (a == "--port") port = val();
    else if (a == "--fps") fps = val();
    else if (a == "--bitrate") bitrate = val();
    else if (a == "--width") width = val();
    else if (a == "--height") height = val();
    else if (a == "--frames") frames = val();
    else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
  }

  if (!test_pattern) {
    std::fprintf(stderr, "Phase 1a: only --test-pattern is wired here. "
                         "evdi source arrives in Task 6.\n");
    return 2;
  }

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

  droppix::TestPatternSource src(width, height, fps);
  droppix::SoftwareEncoder enc;
  droppix::StreamDaemon daemon(src, enc, tx, {fps, bitrate});
  bool ran = daemon.run_until(g_stop, frames);
  return ran ? 0 : 1;
}
