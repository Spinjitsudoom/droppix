#pragma once
#include <cstdint>
#include <map>
#include "encoder.h"
#include "nv12_converter.h"

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace droppix {
// NVIDIA hardware H.264 encoder (h264_nvenc). Takes the CPU-side NV12 frame
// produced by Nv12Converter directly; nvenc uploads it internally, so this
// class is structurally identical to SoftwareEncoder aside from codec name
// and low-latency nvenc-specific options.
class NvencEncoder : public Encoder {
 public:
  ~NvencEncoder() override;
  bool open(int width, int height, int fps, int bitrate_kbps) override;
  std::vector<unsigned char> extradata() const override;
  std::vector<EncodedPacket> encode(const Frame& frame, int64_t pts_us) override;
  std::vector<EncodedPacket> flush() override;
  void request_keyframe() override { force_idr_ = true; }


 private:
  // Free everything open() allocated, so open() can be called again mid-session.
  void reset();

  std::vector<EncodedPacket> drain();  // pull packets from the codec

  AVCodecContext* ctx_ = nullptr;
  Nv12Converter conv_;
  AVPacket* pkt_ = nullptr;
  int width_ = 0, height_ = 0, fps_ = 30;
  int64_t frame_index_ = 0;
  std::map<int64_t, int64_t> pts_map_;  // frame_index (codec pts tick) -> caller pts_us
  bool force_idr_ = false;   // next encode must be an IDR (client lost sync)
};
}  // namespace droppix
