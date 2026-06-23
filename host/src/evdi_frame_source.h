#pragma once
#include <memory>
#include "frame_source.h"
#include "virtual_display.h"
#include "capturer.h"

namespace droppix {
class EvdiFrameSource : public FrameSource {
 public:
  bool start(int& width, int& height) override;
  Frame next(int timeout_ms) override;
 private:
  VirtualDisplay display_;
  std::unique_ptr<Capturer> cap_;
};
}  // namespace droppix
