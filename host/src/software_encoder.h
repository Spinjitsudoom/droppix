#pragma once
#include <cstdint>
#include <map>
#include "encoder.h"

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace droppix {
class SoftwareEncoder : public Encoder {
 public:
  ~SoftwareEncoder() override;
  bool open(int width, int height, int fps, int bitrate_kbps) override;
  std::vector<unsigned char> extradata() const override;
  std::vector<EncodedPacket> encode(const Frame& frame, int64_t pts_us) override;
  std::vector<EncodedPacket> flush() override;

 private:
  std::vector<EncodedPacket> drain();  // pull packets from the codec

  AVCodecContext* ctx_ = nullptr;
  AVFrame* nv12_ = nullptr;
  AVPacket* pkt_ = nullptr;
  SwsContext* sws_ = nullptr;
  int width_ = 0, height_ = 0, fps_ = 30;
  int64_t frame_index_ = 0;
  std::map<int64_t, int64_t> pts_map_;  // frame_index (codec pts tick) -> caller pts_us
};
}  // namespace droppix
