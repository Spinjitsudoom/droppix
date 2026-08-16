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
  // Must exceed the X11 on_connected callback's own budget (link_providers() wraps its
  // xrandr/runuser work in "timeout 10"): runuser's PAM/D-Bus session setup can eat several
  // seconds on its own, so a 5s wait here raced the callback and lost even when it would have
  // succeeded shortly after — the evdi mode-changed event never fires if the callback hasn't
  // enabled the output yet, so wait_for_mode has no way to return early once its poll blocks.
  const int kModeWaitMs = 12000;
  std::thread adopt_thread;
  if (on_connected) adopt_thread = std::thread(on_connected);
  const bool got_mode = cap_->wait_for_mode(kModeWaitMs);
  if (adopt_thread.joinable()) adopt_thread.join();
  if (!got_mode) {
    std::fprintf(stderr, "evdi: no compositor mode within %ds for %dx%d@%d\n",
                 kModeWaitMs / 1000, width_, height_, refresh_hz_);
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
