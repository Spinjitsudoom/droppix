#pragma once
#include "frame_source.h"

namespace droppix {
class TestPatternSource : public FrameSource {
 public:
  TestPatternSource(int width, int height, int fps);
  bool start(int& width, int& height) override;
  Frame next(int timeout_ms) override;
 private:
  int width_, height_, fps_;
  int tick_ = 0;
};
}  // namespace droppix
