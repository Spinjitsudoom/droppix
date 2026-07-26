#pragma once
#include <functional>
#include "capturer.h"  // droppix::Frame

namespace droppix {
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
  virtual bool start(int& width, int& height,
                     const std::function<void()>& on_connected = {}) = 0;
  // Next frame; Frame.valid == false on timeout / no update.
  virtual Frame next(int timeout_ms) = 0;
};
}  // namespace droppix
