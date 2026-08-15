#include "jpeg_stripe_encoder.h"

#include <algorithm>
#include <cstdio>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace droppix {

bool JpegStripeEncoder::open(int width, int height, int quality) {
  close();
  if (width <= 0 || height <= 0) return false;
  width_ = width;
  height_ = height;

  const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
  if (!codec) {
    std::fprintf(stderr, "mjpeg encoder not found\n");
    return false;
  }
  ctx_ = avcodec_alloc_context3(codec);
  if (!ctx_) return false;
  ctx_->width = width;
  ctx_->height = height;
  // YUVJ420P is full-range JPEG YUV — what a baseline JFIF file carries.
  ctx_->pix_fmt = AV_PIX_FMT_YUVJ420P;
  ctx_->color_range = AVCOL_RANGE_JPEG;
  ctx_->time_base = AVRational{1, 60};
  // MJPEG rate control is quality-based: qscale 2 (best) .. 31 (worst).
  quality = std::clamp(quality, 1, 100);
  const int qscale = 31 - (quality * 29) / 100;   // 100 -> 2, 1 -> ~31
  ctx_->flags |= AV_CODEC_FLAG_QSCALE;
  ctx_->global_quality = FF_QP2LAMBDA * qscale;

  if (avcodec_open2(ctx_, codec, nullptr) < 0) {
    std::fprintf(stderr, "avcodec_open2 failed (mjpeg)\n");
    avcodec_free_context(&ctx_);
    return false;
  }

  frame_ = av_frame_alloc();
  pkt_ = av_packet_alloc();
  if (!frame_ || !pkt_) return false;
  frame_->format = ctx_->pix_fmt;
  frame_->width = width;
  frame_->height = height;
  if (av_frame_get_buffer(frame_, 32) < 0) {
    std::fprintf(stderr, "av_frame_get_buffer failed (mjpeg)\n");
    return false;
  }
  return true;
}

std::vector<unsigned char> JpegStripeEncoder::encode(const unsigned char* bgra, int stride,
                                                     int y_offset, int rows) {
  std::vector<unsigned char> out;
  if (!ctx_ || !frame_ || !pkt_ || !bgra) return out;
  if (rows != height_) return out;   // geometry must match open()

  // BGRA -> YUVJ420P. Point the converter at the stripe's first row so no copy is needed.
  sws_ = sws_getCachedContext(sws_, width_, height_, AV_PIX_FMT_BGRA,
                              width_, height_, ctx_->pix_fmt,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
  if (!sws_) return out;
  if (av_frame_make_writable(frame_) < 0) return out;

  const unsigned char* src[4] = {bgra + static_cast<ptrdiff_t>(y_offset) * stride,
                                 nullptr, nullptr, nullptr};
  const int src_stride[4] = {stride, 0, 0, 0};
  sws_scale(sws_, src, src_stride, 0, height_, frame_->data, frame_->linesize);

  frame_->pts = pts_++;
  frame_->quality = ctx_->global_quality;
  if (avcodec_send_frame(ctx_, frame_) < 0) return out;
  // MJPEG is intra-only: exactly one packet per frame, no draining across calls.
  while (avcodec_receive_packet(ctx_, pkt_) == 0) {
    out.insert(out.end(), pkt_->data, pkt_->data + pkt_->size);
    av_packet_unref(pkt_);
  }
  return out;
}

void JpegStripeEncoder::close() {
  if (sws_) { sws_freeContext(sws_); sws_ = nullptr; }
  if (pkt_) av_packet_free(&pkt_);
  if (frame_) av_frame_free(&frame_);
  if (ctx_) avcodec_free_context(&ctx_);
  width_ = height_ = 0;
  pts_ = 0;
}

JpegStripeEncoder::~JpegStripeEncoder() { close(); }

}  // namespace droppix
