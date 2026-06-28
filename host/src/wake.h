#pragma once
#include <cstdint>
#include <vector>

namespace droppix {

inline std::vector<unsigned char> encode_wake(uint16_t port) {
  return {'D', 'P', 'X', 'W', (unsigned char)(port >> 8), (unsigned char)(port & 0xFF)};
}

inline bool decode_wake(const std::vector<unsigned char>& b, uint16_t& port) {
  if (b.size() != 6 || b[0] != 'D' || b[1] != 'P' || b[2] != 'X' || b[3] != 'W') return false;
  port = (uint16_t(b[4]) << 8) | b[5];
  return true;
}

}  // namespace droppix
