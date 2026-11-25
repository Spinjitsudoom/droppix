#pragma once
#include <cstdint>
#include <functional>
#include "capturer.h"  // droppix::Frame

namespace droppix {

// True when a captured frame can be encoded by the session it arrived in: same size as the
// session, and its pixel buffer really holds stride*height bytes.
inline bool frame_fits_session(const Frame& f, int w, int h) {
  return f.width == w && f.height == h && f.stride >= w * 4 &&
         f.bgra.size() >= static_cast<size_t>(f.stride) * static_cast<size_t>(f.height);
}

class FrameSource {
 public:
  virtual ~FrameSource() = default;
  // Begin producing; outputs the chosen frame dimensions. Returns success.
  //
  // on_connected (optional): invoked once the underlying display has been created
  // and connected but BEFORE start() blocks waiting for the compositor to assign it
  // a mode. The evdi source runs it on a background thread so backends that must
  // enable/place the new output themselves (X11 reverse-PRIME) can do so while the
  // mode-wait pumps the compositor's probe events — otherwise the wait and the
  // enable deadlock. Sources that don't need it (test pattern) ignore it.
  //
  // on_stalled (optional): invoked (on a background thread) if no mode has arrived a few
  // seconds into the wait. GNOME can believe a reconnected output is still on from the
  // previous session and never send it a mode; the backend's revive_output() nudges it.
  virtual bool start(int& width, int& height,
                     const std::function<void()>& on_connected = {},
                     const std::function<void()>& on_stalled = {}) = 0;
  // EDID serial of the display this source created (0 = none). Lets the desktop backend
  // find exactly this session's output by identity rather than by what appeared when.
  virtual uint32_t edid_serial() const { return 0; }
  // Next frame; Frame.valid == false on timeout / no update.
  virtual Frame next(int timeout_ms) = 0;
};
}  // namespace droppix
