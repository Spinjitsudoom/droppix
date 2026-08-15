#include "vaapi_encoder.h"
#include <algorithm>
#include <cstdio>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/hwcontext.h>
}

namespace droppix {

void VaapiEncoder::free_attempt() {
  if (ctx_) avcodec_free_context(&ctx_);
  if (hw_frames_) av_buffer_unref(&hw_frames_);
  if (hw_device_) av_buffer_unref(&hw_device_);
}

bool VaapiEncoder::open(int width, int height, int fps, int bitrate_kbps) {
  width_ = width; height_ = height; fps_ = fps;

  const AVCodec* codec = avcodec_find_encoder_by_name("h264_vaapi");
  if (!codec) { std::fprintf(stderr, "h264_vaapi encoder not found\n"); return false; }

  const char* nodes[] = {
      nullptr,  // default device resolution
      "/dev/dri/renderD128", "/dev/dri/renderD129", "/dev/dri/renderD130",
      "/dev/dri/renderD131", "/dev/dri/renderD132", "/dev/dri/renderD133",
      "/dev/dri/renderD134", "/dev/dri/renderD135",
  };

  // One full open attempt (device + frames ctx + codec ctx + avcodec_open2)
  // for a given node/low_power combo. On any failure this tears the whole
  // attempt down via free_attempt() so the next attempt starts clean.
  auto try_open = [&](const char* node, bool low_power) -> bool {
    if (av_hwdevice_ctx_create(&hw_device_, AV_HWDEVICE_TYPE_VAAPI, node, nullptr, 0) < 0) {
      hw_device_ = nullptr;
      return false;
    }

    // Frames ctx: VAAPI hw surfaces backed by NV12 sw layout.
    hw_frames_ = av_hwframe_ctx_alloc(hw_device_);
    if (!hw_frames_) { free_attempt(); return false; }
    auto* fc = reinterpret_cast<AVHWFramesContext*>(hw_frames_->data);
    fc->format = AV_PIX_FMT_VAAPI;
    fc->sw_format = AV_PIX_FMT_NV12;
    fc->width = width;
    fc->height = height;
    if (av_hwframe_ctx_init(hw_frames_) < 0) { free_attempt(); return false; }

    ctx_ = avcodec_alloc_context3(codec);
    if (!ctx_) { free_attempt(); return false; }
    ctx_->width = width;
    ctx_->height = height;
    ctx_->pix_fmt = AV_PIX_FMT_VAAPI;
    ctx_->time_base = AVRational{1, fps};
    ctx_->framerate = AVRational{fps, 1};
    ctx_->gop_size = fps * 2;        // keyframe every ~2s
    ctx_->max_b_frames = 0;          // no B-frames: lowest latency
    ctx_->bit_rate = int64_t(bitrate_kbps) * 1000;
    // VBV: cap instantaneous bitrate so single frames can't balloon (low latency).
    ctx_->rc_max_rate = ctx_->bit_rate;
    ctx_->rc_buffer_size = static_cast<int>(ctx_->bit_rate / std::max(1, fps) * 2);  // ~2 frames
    ctx_->hw_frames_ctx = av_buffer_ref(hw_frames_);
    av_opt_set(ctx_->priv_data, "rc_mode", "CBR", 0);
    // Newer Intel iGPUs (e.g. Xe/Arc) only expose the low-power H.264 encode
    // entrypoint (VAEntrypointEncSliceLP); a plain h264_vaapi open fails
    // there unless low_power is explicitly requested.
    if (low_power) av_opt_set_int(ctx_->priv_data, "low_power", 1, 0);
    // NO AV_CODEC_FLAG_GLOBAL_HEADER -> SPS/PPS repeated in-band per IDR.

    if (avcodec_open2(ctx_, codec, nullptr) == 0) return true;
    // Failed on this attempt (e.g. default resolved to an NVDEC decode-only
    // backend, or the node lacks the requested entrypoint) -> free
    // everything from this attempt so the next one starts clean.
    free_attempt();
    return false;
  };

  const char* winning_node = nullptr;
  bool winning_low_power = false;
  for (const char* node : nodes) {
    if (try_open(node, false)) { winning_node = node; winning_low_power = false; break; }
    if (try_open(node, true)) { winning_node = node; winning_low_power = true; break; }
  }

  if (!ctx_ || !avcodec_is_open(ctx_)) return false;

  std::fprintf(stderr, "encoder: using vaapi (%s%s)\n",
               winning_node ? winning_node : "(default)",
               winning_low_power ? ", low_power" : "");

  if (!conv_.open(width, height)) { free_attempt(); return false; }

  vaapi_frame_ = av_frame_alloc();
  pkt_ = av_packet_alloc();
  return vaapi_frame_ != nullptr && pkt_ != nullptr;
}

std::vector<unsigned char> VaapiEncoder::extradata() const {
  if (!ctx_ || !ctx_->extradata || ctx_->extradata_size <= 0) return {};
  return std::vector<unsigned char>(ctx_->extradata,
                                    ctx_->extradata + ctx_->extradata_size);
}

std::vector<EncodedPacket> VaapiEncoder::drain() {
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

std::vector<EncodedPacket> VaapiEncoder::encode(const Frame& frame, int64_t pts_us) {
  if (!ctx_ || !frame.valid) return {};
  AVFrame* nv12 = conv_.convert(frame);  // BGRA -> NV12 (CPU side)

  av_frame_unref(vaapi_frame_);
  if (av_hwframe_get_buffer(hw_frames_, vaapi_frame_, 0) < 0) return {};
  if (av_hwframe_transfer_data(vaapi_frame_, nv12, 0) < 0) return {};

  int64_t idx = frame_index_++;
  vaapi_frame_->pts = idx;       // codec time_base (1/fps) tick
  // A client that lost sync waits out the whole GOP otherwise (2s at the default
  // gop_size); forcing an I-picture recovers it on the very next frame.
  vaapi_frame_->pict_type = force_idr_ ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
  force_idr_ = false;
  pts_map_[idx] = pts_us;        // remember the caller's microsecond timestamp
  if (avcodec_send_frame(ctx_, vaapi_frame_) < 0) return {};
  return drain();
}

std::vector<EncodedPacket> VaapiEncoder::flush() {
  if (!ctx_) return {};
  avcodec_send_frame(ctx_, nullptr);  // enter draining mode
  auto out = drain();
  pts_map_.clear();
  return out;
}

VaapiEncoder::~VaapiEncoder() {
  if (pkt_) av_packet_free(&pkt_);
  if (vaapi_frame_) av_frame_free(&vaapi_frame_);
  if (ctx_) avcodec_free_context(&ctx_);
  if (hw_frames_) av_buffer_unref(&hw_frames_);
  if (hw_device_) av_buffer_unref(&hw_device_);
}

}  // namespace droppix
