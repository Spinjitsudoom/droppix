#pragma once
#include <cstdint>
#include <vector>

namespace droppix {
struct EncodedPacket {
  std::vector<unsigned char> data;  // Annex-B H.264 access unit
  int64_t pts_us = 0;
  bool keyframe = false;
};
}  // namespace droppix
