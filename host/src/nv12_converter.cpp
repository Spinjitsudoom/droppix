#include "nv12_converter.h"
#include <cstdint>
#include "capturer.h"
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace droppix {

bool Nv12Converter::open(int w, int h) {
  w_ = w; h_ = h;

  nv12_ = av_frame_alloc();
  if (!nv12_) return false;
  nv12_->format = AV_PIX_FMT_NV12;
  nv12_->width = w;
  nv12_->height = h;
  if (av_frame_get_buffer(nv12_, 32) < 0) return false;

  sws_ = sws_getContext(w, h, AV_PIX_FMT_BGRA,
                         w, h, AV_PIX_FMT_NV12,
                         SWS_BILINEAR, nullptr, nullptr, nullptr);
  return sws_ != nullptr;
}

AVFrame* Nv12Converter::convert(const Frame& bgra) {
  const uint8_t* src[1] = { bgra.bgra.data() };
  int src_stride[1] = { bgra.stride };
  sws_scale(sws_, src, src_stride, 0, h_, nv12_->data, nv12_->linesize);
  return nv12_;
}

Nv12Converter::~Nv12Converter() {
  if (sws_) sws_freeContext(sws_);
  if (nv12_) av_frame_free(&nv12_);
}

}  // namespace droppix
