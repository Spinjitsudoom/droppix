#pragma once

struct AVFrame;
struct SwsContext;

namespace droppix {

struct Frame;  // capturer.h

// Shared BGRA -> NV12 colorspace/scale conversion (sws_scale wrapper).
// Owns a SwsContext and a reusable CPU-side NV12 AVFrame; convert() scales
// into that frame and returns it (the caller owns pts/queueing bookkeeping).
class Nv12Converter {
 public:
  ~Nv12Converter();
  bool open(int w, int h);
  AVFrame* convert(const Frame& bgra);

 private:
  int w_ = 0, h_ = 0;
  SwsContext* sws_ = nullptr;
  AVFrame* nv12_ = nullptr;
};

}  // namespace droppix
