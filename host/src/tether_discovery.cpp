#include "tether_discovery.h"

#include <algorithm>

namespace droppix {

std::vector<unsigned char> encode_reply(const TetherReply& r) {
  std::vector<unsigned char> b = {'D','P','X','R',
    (unsigned char)(r.wake_port >> 8), (unsigned char)(r.wake_port & 0xFF)};
  const size_t idn = std::min<size_t>(r.id.size(), 255);
  b.push_back((unsigned char)idn);
  b.insert(b.end(), r.id.begin(), r.id.begin() + idn);
  const size_t nn = std::min<size_t>(r.name.size(), 255);
  b.push_back((unsigned char)nn);
  b.insert(b.end(), r.name.begin(), r.name.begin() + nn);
  return b;
}

bool decode_reply(const std::vector<unsigned char>& b, TetherReply& out) {
  if (b.size() < 7 || b[0]!='D'||b[1]!='P'||b[2]!='X'||b[3]!='R') return false;
  out.wake_port = (uint16_t(b[4]) << 8) | b[5];
  size_t i = 6;
  size_t idLen = b[i++];
  if (i + idLen > b.size()) return false;
  out.id.assign(b.begin() + i, b.begin() + i + idLen); i += idLen;
  if (i >= b.size()) return false;
  size_t nameLen = b[i++];
  if (i + nameLen > b.size()) return false;
  out.name.assign(b.begin() + i, b.begin() + i + nameLen);
  return true;
}

}  // namespace droppix
