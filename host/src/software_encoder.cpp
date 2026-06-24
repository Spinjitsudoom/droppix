#include "software_encoder.h"
#include <algorithm>
#include <cstdio>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace droppix {

bool SoftwareEncoder::open(int width, int height, int fps, int bitrate_kbps) {
  width_ = width; height_ = height; fps_ = fps;
  const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
  if (!codec) { std::fprintf(stderr, "libx264 encoder not found\n"); return false; }

  ctx_ = avcodec_alloc_context3(codec);
  if (!ctx_) return false;
  ctx_->width = width;
  ctx_->height = height;
  ctx_->pix_fmt = AV_PIX_FMT_NV12;
  ctx_->time_base = AVRational{1, fps};
  ctx_->framerate = AVRational{fps, 1};
  ctx_->gop_size = fps * 2;        // keyframe every ~2s
  ctx_->max_b_frames = 0;          // no B-frames: lowest latency
  ctx_->bit_rate = int64_t(bitrate_kbps) * 1000;
  // VBV: cap instantaneous bitrate so single frames can't balloon (low latency).
  ctx_->rc_max_rate = ctx_->bit_rate;
  ctx_->rc_buffer_size = static_cast<int>(ctx_->bit_rate / std::max(1, fps) * 2);  // ~2 frames
  av_opt_set(ctx_->priv_data, "preset", "ultrafast", 0);
  av_opt_set(ctx_->priv_data, "tune", "zerolatency", 0);
  // Make every IDR self-contained (SPS/PPS repeated in-band), and pin the x264
  // VBV params explicitly: with some libavcodec/libx264 builds, setting
  // ctx_->rc_max_rate / rc_buffer_size alone is not reliably translated into
  // x264's vbv-maxrate/vbv-bufsize, so we also pass them via x264-params.
  {
    // Buffer size in kbits, sized to hold ~2 frames worth of data at the
    // target bitrate (matches rc_buffer_size above, expressed in kbits).
    const int vbv_bufsize_kbits = std::max(1, bitrate_kbps * 2 / std::max(1, fps));
    char params[160];
    std::snprintf(params, sizeof(params),
                   "repeat-headers=1:vbv-maxrate=%d:vbv-bufsize=%d",
                   bitrate_kbps, vbv_bufsize_kbits);
    av_opt_set(ctx_->priv_data, "x264-params", params, 0);
  }

  if (avcodec_open2(ctx_, codec, nullptr) < 0) {
    std::fprintf(stderr, "avcodec_open2 failed\n"); return false;
  }

  nv12_ = av_frame_alloc();
  if (!nv12_) return false;
  nv12_->format = AV_PIX_FMT_NV12;
  nv12_->width = width;
  nv12_->height = height;
  if (av_frame_get_buffer(nv12_, 32) < 0) return false;

  pkt_ = av_packet_alloc();
  if (!pkt_) return false;

  sws_ = sws_getContext(width, height, AV_PIX_FMT_BGRA,
                        width, height, AV_PIX_FMT_NV12,
                        SWS_BILINEAR, nullptr, nullptr, nullptr);
  return sws_ != nullptr;
}

std::vector<unsigned char> SoftwareEncoder::extradata() const {
  if (!ctx_ || !ctx_->extradata || ctx_->extradata_size <= 0) return {};
  return std::vector<unsigned char>(ctx_->extradata,
                                    ctx_->extradata + ctx_->extradata_size);
}

std::vector<EncodedPacket> SoftwareEncoder::drain() {
  std::vector<EncodedPacket> out;
  for (;;) {
    int r = avcodec_receive_packet(ctx_, pkt_);
    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
    if (r < 0) { std::fprintf(stderr, "receive_packet error\n"); break; }
    EncodedPacket ep;
    ep.data.assign(pkt_->data, pkt_->data + pkt_->size);
    auto it = pts_map_.find(pkt_->pts);   // map the codec tick back to caller us
    if (it != pts_map_.end()) { ep.pts_us = it->second; pts_map_.erase(it); }
    else { ep.pts_us = 0; }
    ep.keyframe = (pkt_->flags & AV_PKT_FLAG_KEY) != 0;
    out.push_back(std::move(ep));
    av_packet_unref(pkt_);
  }
  return out;
}

std::vector<EncodedPacket> SoftwareEncoder::encode(const Frame& frame, int64_t pts_us) {
  if (!ctx_ || !frame.valid) return {};
  // BGRA -> NV12.
  const uint8_t* src[1] = { frame.bgra.data() };
  int src_stride[1] = { frame.stride };
  sws_scale(sws_, src, src_stride, 0, height_, nv12_->data, nv12_->linesize);
  int64_t idx = frame_index_++;
  nv12_->pts = idx;            // codec time_base (1/fps) tick
  pts_map_[idx] = pts_us;      // remember the caller's microsecond timestamp
  if (avcodec_send_frame(ctx_, nv12_) < 0) return {};
  return drain();
}

std::vector<EncodedPacket> SoftwareEncoder::flush() {
  if (!ctx_) return {};
  avcodec_send_frame(ctx_, nullptr);  // enter draining mode
  auto out = drain();
  pts_map_.clear();
  return out;
}

SoftwareEncoder::~SoftwareEncoder() {
  if (sws_) sws_freeContext(sws_);
  if (pkt_) av_packet_free(&pkt_);
  if (nv12_) av_frame_free(&nv12_);
  if (ctx_) avcodec_free_context(&ctx_);
}

}  // namespace droppix
