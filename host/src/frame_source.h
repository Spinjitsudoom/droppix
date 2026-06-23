#pragma once
#include "capturer.h"  // droppix::Frame

namespace droppix {
class FrameSource {
 public:
  virtual ~FrameSource() = default;
  // Begin producing; outputs the chosen frame dimensions. Returns success.
  virtual bool start(int& width, int& height) = 0;
  // Next frame; Frame.valid == false on timeout / no update.
  virtual Frame next(int timeout_ms) = 0;
};
}  // namespace droppix
