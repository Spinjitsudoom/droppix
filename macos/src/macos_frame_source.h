#pragma once
#include <condition_variable>
#include <mutex>
#include "frame_source.h"
#include "macos_virtual_display.h"

namespace droppix {

// FrameSource backed by a real CGVirtualDisplay: macOS treats it as a second
// monitor, and CGDisplayStream captures its pixel content. The capture
// callback runs on a dispatch queue, so next() blocks on a condvar that
// callback signals — mirroring how EvdiFrameSource::next() blocks on evdi's
// poll() under the hood.
class MacFrameSource : public FrameSource {
 public:
  MacFrameSource(int width, int height, int refresh_hz)
      : width_(width), height_(height), refresh_hz_(refresh_hz) {}
  ~MacFrameSource() override;
  bool start(int& width, int& height) override;
  Frame next(int timeout_ms) override;
  int native_display_id() const override;

 private:
  int width_, height_, refresh_hz_;
  MacVirtualDisplay display_;
  void* stream_ = nullptr;  // CGDisplayStreamRef

  std::mutex mu_;
  std::condition_variable cv_;
  Frame pending_;
  bool have_pending_ = false;

  void on_frame(Frame f);
};

}  // namespace droppix
