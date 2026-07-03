#include "evdi_frame_source.h"
#include "edid.h"
#include "cvt.h"
#include <cstdio>

namespace droppix {

bool EvdiFrameSource::start(int& width, int& height) {
  if (!display_.open()) return false;
  display_.connect(build_edid(mode_timing(width_, height_, refresh_hz_), serial_));
  cap_ = std::make_unique<Capturer>(display_.handle());
  if (!cap_->wait_for_mode(5000)) {
    std::fprintf(stderr, "evdi: no KWin mode within 5s for %dx%d@%d\n",
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
