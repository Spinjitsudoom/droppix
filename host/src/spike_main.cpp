#include <csignal>
#include <cstdio>
#include <string>
#include <unistd.h>
#include "edid.h"
#include "virtual_display.h"
#include "capturer.h"
#include "png_writer.h"

static volatile std::sig_atomic_t g_stop = 0;
static void on_sigint(int) { g_stop = 1; }

int main(int argc, char** argv) {
  std::signal(SIGINT, on_sigint);
  const int frames = (argc > 1) ? std::atoi(argv[1]) : 10;

  droppix::VirtualDisplay display;
  if (!display.open()) return 1;
  display.connect(droppix::build_edid(droppix::timing_1080p60()));
  std::fprintf(stderr, "Connected on evdi node %d. Waiting for KWin mode...\n",
               display.node());

  droppix::Capturer cap(display.handle());
  if (!cap.wait_for_mode(5000)) {
    std::fprintf(stderr, "No mode within 5s. Is KWin extending onto it?\n");
    return 2;
  }
  std::fprintf(stderr,
      "Mode %dx%d. Drag a window onto the new monitor, then watch frames.\n",
      cap.width(), cap.height());

  int saved = 0;
  for (int i = 0; i < frames && !g_stop; ++i) {
    droppix::Frame f = cap.grab(1000);
    if (!f.valid) { std::fprintf(stderr, "frame %d: timeout\n", i); continue; }
    std::string path = "frame_" + std::to_string(i) + ".png";
    if (droppix::save_png_from_bgra(path, f)) {
      std::fprintf(stderr, "saved %s (%zu dirty rects)\n",
                   path.c_str(), f.rects.size());
      ++saved;
    }
    usleep(200 * 1000);  // 5 fps sampling for the spike
  }
  std::fprintf(stderr, "Done. %d/%d frames saved.\n", saved, frames);
  return saved > 0 ? 0 : 3;
}
