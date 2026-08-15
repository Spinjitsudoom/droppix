#pragma once
#include <cstdint>
#include <map>
#include "encoder.h"
#include "nv12_converter.h"

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct AVBufferRef;

namespace droppix {
// VAAPI hardware H.264 encoder (h264_vaapi). open() probes render nodes
// (default resolution, then /dev/dri/renderD128..135) for one that can
// actually open an h264_vaapi encode context -- on hybrid-GPU systems the
// default node often resolves to an NVDEC decode-only backend, so the probe
// self-selects the encode-capable (usually Intel iGPU) node. Per frame, the
// CPU-side NV12 produced by Nv12Converter is uploaded into a VAAPI surface
// (drawn from a hw frames pool) before being sent to the encoder.
class VaapiEncoder : public Encoder {
 public:
  ~VaapiEncoder() override;
  bool open(int width, int height, int fps, int bitrate_kbps) override;
  std::vector<unsigned char> extradata() const override;
  std::vector<EncodedPacket> encode(const Frame& frame, int64_t pts_us) override;
  std::vector<EncodedPacket> flush() override;
  void request_keyframe() override { force_idr_ = true; }

 private:
  std::vector<EncodedPacket> drain();  // pull packets from the codec
  void free_attempt();                 // free ctx_/hw_frames_/hw_device_, null them

  AVCodecContext* ctx_ = nullptr;
  AVBufferRef* hw_device_ = nullptr;
  AVBufferRef* hw_frames_ = nullptr;
  AVPacket* pkt_ = nullptr;
  Nv12Converter conv_;
  AVFrame* vaapi_frame_ = nullptr;  // reused per-encode hw surface target
  int width_ = 0, height_ = 0, fps_ = 30;
  int64_t frame_index_ = 0;
  std::map<int64_t, int64_t> pts_map_;  // frame_index (codec pts tick) -> caller pts_us
  bool force_idr_ = false;   // next encode must be an IDR (client lost sync)
};
}  // namespace droppix
