#pragma once
#include <vector>
#include "encoded_packet.h"
#include "capturer.h"  // droppix::Frame

namespace droppix {
class Encoder {
 public:
  virtual ~Encoder() = default;
  virtual bool open(int width, int height, int fps, int bitrate_kbps) = 0;
  // SPS/PPS for the CONFIG message; may be empty if headers are in-band.
  virtual std::vector<unsigned char> extradata() const = 0;
  virtual std::vector<EncodedPacket> encode(const Frame& frame, int64_t pts_us) = 0;
  virtual std::vector<EncodedPacket> flush() = 0;
};
}  // namespace droppix
