#include "nvenc_encoder.h"
#include <algorithm>
#include <cstdio>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

namespace droppix {

void NvencEncoder::reset() {
  if (pkt_) av_packet_free(&pkt_);
  if (ctx_) avcodec_free_context(&ctx_);
  pts_map_.clear();
  frame_index_ = 0;
}

bool NvencEncoder::open(int width, int height, int fps, int bitrate_kbps) {
  // Safe to call again mid-session: without this a live fps/bitrate change would
  // allocate a fresh context over the old one and leak the codec, packet and
  // (vaapi) hw frames every time.
  reset();
  width_ = width; height_ = height; fps_ = fps;
  const AVCodec* codec = avcodec_find_encoder_by_name("h264_nvenc");
  if (!codec) { std::fprintf(stderr, "h264_nvenc encoder not found\n"); return false; }

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

  av_opt_set(ctx_->priv_data, "preset", "p4", 0);   // low-latency preset
  av_opt_set(ctx_->priv_data, "tune", "ll", 0);     // low latency
  av_opt_set(ctx_->priv_data, "rc", "cbr", 0);
  av_opt_set_int(ctx_->priv_data, "delay", 0, 0);
  av_opt_set_int(ctx_->priv_data, "zerolatency", 1, 0);
  // NO AV_CODEC_FLAG_GLOBAL_HEADER -> SPS/PPS repeated in-band per IDR.

  if (avcodec_open2(ctx_, codec, nullptr) < 0) {
    std::fprintf(stderr, "avcodec_open2 failed (h264_nvenc)\n");
    avcodec_free_context(&ctx_);
    return false;
  }

  if (!conv_.open(width, height)) return false;

  pkt_ = av_packet_alloc();
  if (!pkt_) return false;

  std::fprintf(stderr, "encoder: using nvenc\n");
  return true;
}

std::vector<unsigned char> NvencEncoder::extradata() const {
  if (!ctx_ || !ctx_->extradata || ctx_->extradata_size <= 0) return {};
  return std::vector<unsigned char>(ctx_->extradata,
                                    ctx_->extradata + ctx_->extradata_size);
}

std::vector<EncodedPacket> NvencEncoder::drain() {
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

std::vector<EncodedPacket> NvencEncoder::encode(const Frame& frame, int64_t pts_us) {
  if (!ctx_ || !frame.valid) return {};
  AVFrame* nv12 = conv_.convert(frame);  // BGRA -> NV12
  int64_t idx = frame_index_++;
  nv12->pts = idx;              // codec time_base (1/fps) tick
  // A client that lost sync waits out the whole GOP otherwise (2s at the default
  // gop_size); forcing an I-picture recovers it on the very next frame.
  nv12->pict_type = force_idr_ ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
  force_idr_ = false;
  pts_map_[idx] = pts_us;       // remember the caller's microsecond timestamp
  if (avcodec_send_frame(ctx_, nv12) < 0) return {};
  return drain();
}

std::vector<EncodedPacket> NvencEncoder::flush() {
  if (!ctx_) return {};
  avcodec_send_frame(ctx_, nullptr);  // enter draining mode
  auto out = drain();
  pts_map_.clear();
  return out;
}

NvencEncoder::~NvencEncoder() { reset(); }

}  // namespace droppix
