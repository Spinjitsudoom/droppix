#include "test_pattern_source.h"
#include <ctime>

namespace droppix {

TestPatternSource::TestPatternSource(int width, int height, int fps)
    : width_(width), height_(height), fps_(fps) {}

bool TestPatternSource::start(int& width, int& height) {
  width = width_; height = height_;
  return true;
}

Frame TestPatternSource::next(int timeout_ms) {
  // Pace to ~fps so the stream isn't faster than real time.
  if (tick_ > 0 && fps_ > 0) {
    struct timespec ts{0, (1000L * 1000L * 1000L) / fps_};
    nanosleep(&ts, nullptr);
  }
  (void)timeout_ms;
  Frame f;
  f.width = width_; f.height = height_; f.stride = width_ * 4; f.valid = true;
  f.bgra.resize(static_cast<size_t>(width_) * height_ * 4);
  const int t = tick_++;
  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      size_t i = (static_cast<size_t>(y) * width_ + x) * 4;
      f.bgra[i + 0] = static_cast<unsigned char>((x + t * 4) & 0xFF);  // B
      f.bgra[i + 1] = static_cast<unsigned char>((y + t * 2) & 0xFF);  // G
      f.bgra[i + 2] = static_cast<unsigned char>((x + y + t) & 0xFF);  // R
      f.bgra[i + 3] = 0xFF;
    }
  }
  return f;
}

}  // namespace droppix
