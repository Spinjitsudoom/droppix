#include "evdi_frame_source.h"
#include "edid.h"
#include "cvt.h"
#include <cstdio>
#include <thread>

namespace droppix {

bool EvdiFrameSource::start(int& width, int& height,
                            const std::function<void()>& on_connected) {
  if (!display_.open()) return false;
  display_.connect(build_edid(mode_timing(width_, height_, refresh_hz_), serial_));
  cap_ = std::make_unique<Capturer>(display_.handle());
  // Some backends (X11 reverse-PRIME) must enable/place the freshly-connected output
  // themselves before the compositor assigns it a mode. Run that on a background
  // thread so wait_for_mode below keeps pumping evdi events — the compositor's output
  // probes are answered by this pump, and the mode-set the callback triggers is what
  // ends the wait. Running the callback after the wait would deadlock (the wait needs
  // the mode the callback produces). The callback only touches the compositor via
  // subprocesses (xrandr), never the evdi handle, so it can't race this thread's
  // evdi_handle_events.
  std::thread adopt_thread;
  if (on_connected) adopt_thread = std::thread(on_connected);
  const bool got_mode = cap_->wait_for_mode(5000);
  if (adopt_thread.joinable()) adopt_thread.join();
  if (!got_mode) {
    std::fprintf(stderr, "evdi: no compositor mode within 5s for %dx%d@%d\n",
                 width_, height_, refresh_hz_);
    cap_.reset();
    display_.disconnect();
    return false;
  }
  width = cap_->width();
  height = cap_->height();
  return true;
}

Frame EvdiFrameSource::next(int timeout_ms) {
  if (!cap_) return Frame{};
  return cap_->grab(timeout_ms);
}

}  // namespace droppix
