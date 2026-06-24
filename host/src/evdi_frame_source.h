#pragma once
#include <memory>
#include "frame_source.h"
#include "virtual_display.h"
#include "capturer.h"

namespace droppix {
class EvdiFrameSource : public FrameSource {
 public:
  EvdiFrameSource(int width, int height, int refresh_hz)
      : width_(width), height_(height), refresh_hz_(refresh_hz) {}
  bool start(int& width, int& height) override;
  Frame next(int timeout_ms) override;
 private:
  int width_, height_, refresh_hz_;
  VirtualDisplay display_;
  std::unique_ptr<Capturer> cap_;
};
}  // namespace droppix
