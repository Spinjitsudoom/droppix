#include "macos_frame_source.h"
#include <CoreGraphics/CGDisplayStream.h>
#include <CoreVideo/CVPixelBuffer.h>
#include <IOSurface/IOSurface.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace droppix {

MacFrameSource::~MacFrameSource() {
  if (stream_) {
    CGDisplayStreamRef stream = (CGDisplayStreamRef)stream_;
    CGDisplayStreamStop(stream);
    CFRelease(stream);
    stream_ = nullptr;
  }
  // display_'s own destructor tears down the virtual display after the
  // stream above has stopped reading from it.
}

void MacFrameSource::on_frame(Frame f) {
  std::lock_guard<std::mutex> lk(mu_);
  pending_ = std::move(f);
  have_pending_ = true;
  cv_.notify_one();
}

bool MacFrameSource::start(int& width, int& height) {
  if (!display_.open(width_, height_, refresh_hz_)) {
    std::fprintf(stderr, "macos: CGVirtualDisplay creation failed for %dx%d@%d "
                 "(check Screen Recording permission)\n", width_, height_, refresh_hz_);
    return false;
  }
  width = width_;
  height = height_;

  CGDirectDisplayID did = display_.display_id();
  // applySettings() returns before WindowServer has actually adopted the new
  // mode (CGDisplayBounds briefly reports 1x1) — poll until it converges,
  // same shape as EvdiFrameSource waiting on KWin's mode-changed event.
  {
    const int deadline_ms = 5000, step_ms = 50;
    int waited = 0;
    while (waited < deadline_ms) {
      CGRect bounds = CGDisplayBounds(did);
      if (static_cast<int>(bounds.size.width) == width_ &&
          static_cast<int>(bounds.size.height) == height_) {
        break;
      }
      usleep(step_ms * 1000);
      waited += step_ms;
    }
    if (waited >= deadline_ms) {
      std::fprintf(stderr, "macos: display mode did not converge to %dx%d within %dms\n",
                   width_, height_, deadline_ms);
    }
  }
  dispatch_queue_t queue = dispatch_queue_create("droppix.frame_source", DISPATCH_QUEUE_SERIAL);

  CGDisplayStreamFrameAvailableHandler handler =
      ^(CGDisplayStreamFrameStatus status, uint64_t /*displayTime*/,
        IOSurfaceRef frameSurface, CGDisplayStreamUpdateRef updateRef) {
        if (status != kCGDisplayStreamFrameStatusFrameComplete || !frameSurface) return;

        IOSurfaceLock(frameSurface, kIOSurfaceLockReadOnly, nullptr);
        Frame f;
        f.width = static_cast<int>(IOSurfaceGetWidth(frameSurface));
        f.height = static_cast<int>(IOSurfaceGetHeight(frameSurface));
        f.stride = static_cast<int>(IOSurfaceGetBytesPerRow(frameSurface));
        const unsigned char* base =
            static_cast<const unsigned char*>(IOSurfaceGetBaseAddress(frameSurface));
        f.bgra.assign(base, base + static_cast<size_t>(f.stride) * f.height);
        IOSurfaceUnlock(frameSurface, kIOSurfaceLockReadOnly, nullptr);

        size_t rect_count = 0;
        const CGRect* rects = CGDisplayStreamUpdateGetRects(
            updateRef, kCGDisplayStreamUpdateDirtyRects, &rect_count);
        for (size_t i = 0; i < rect_count; ++i) {
          const CGRect& r = rects[i];
          f.rects.push_back(evdi_rect{
              static_cast<int>(r.origin.x), static_cast<int>(r.origin.y),
              static_cast<int>(r.origin.x + r.size.width),
              static_cast<int>(r.origin.y + r.size.height)});
        }
        f.valid = true;
        this->on_frame(std::move(f));
      };

  CGDisplayStreamRef stream = CGDisplayStreamCreateWithDispatchQueue(
      did, static_cast<size_t>(width_), static_cast<size_t>(height_),
      kCVPixelFormatType_32BGRA, nullptr, queue, handler);
  if (!stream) {
    std::fprintf(stderr, "macos: CGDisplayStreamCreate failed\n");
    return false;
  }
  if (CGDisplayStreamStart(stream) != kCGErrorSuccess) {
    std::fprintf(stderr, "macos: CGDisplayStreamStart failed\n");
    CFRelease(stream);
    return false;
  }
  stream_ = (void*)stream;
  return true;
}

Frame MacFrameSource::next(int timeout_ms) {
  std::unique_lock<std::mutex> lk(mu_);
  if (!cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                     [this] { return have_pending_; })) {
    return Frame{};  // valid == false: timeout, same as evdi's "no update"
  }
  Frame f = std::move(pending_);
  have_pending_ = false;
  return f;
}

int MacFrameSource::native_display_id() const {
  return static_cast<int>(display_.display_id());
}

}  // namespace droppix
