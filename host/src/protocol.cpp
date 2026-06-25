#include "protocol.h"

namespace droppix {
namespace {

void put_u32(std::vector<unsigned char>& v, uint32_t x) {
  v.push_back((x >> 24) & 0xFF); v.push_back((x >> 16) & 0xFF);
  v.push_back((x >> 8) & 0xFF);  v.push_back(x & 0xFF);
}
void put_u64(std::vector<unsigned char>& v, uint64_t x) {
  for (int s = 56; s >= 0; s -= 8) v.push_back((x >> s) & 0xFF);
}
uint32_t get_u32(const unsigned char* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
uint64_t get_u64(const unsigned char* p) {
  uint64_t x = 0;
  for (int i = 0; i < 8; ++i) x = (x << 8) | p[i];
  return x;
}
void put_u16(std::vector<unsigned char>& v, uint16_t x) {
  v.push_back((x >> 8) & 0xFF); v.push_back(x & 0xFF);
}
uint16_t get_u16(const unsigned char* p) {
  return (uint16_t(p[0]) << 8) | uint16_t(p[1]);
}

}  // namespace

std::vector<unsigned char> encode_message(MsgType type,
                                          const std::vector<unsigned char>& body) {
  std::vector<unsigned char> m;
  uint32_t len = 1 + static_cast<uint32_t>(body.size());
  put_u32(m, len);
  m.push_back(static_cast<unsigned char>(type));
  m.insert(m.end(), body.begin(), body.end());
  return m;
}

void MessageParser::feed(const unsigned char* data, size_t n) {
  buf_.insert(buf_.end(), data, data + n);
}

bool MessageParser::next(ParsedMessage& out) {
  static constexpr uint32_t kMaxMessage = 64u * 1024u * 1024u;  // 64 MiB sanity cap
  for (;;) {
    if (buf_.size() - pos_ < 4) return false;
    uint32_t len = get_u32(buf_.data() + pos_);
    if (len < 1 || len > kMaxMessage) {
      pos_ += 4;       // malformed length word: skip and resync (no recursion)
      continue;
    }
    if (buf_.size() - pos_ < static_cast<size_t>(4) + len) return false;  // widened
    const unsigned char* p = buf_.data() + pos_ + 4;
    out.type = static_cast<MsgType>(p[0]);
    out.body.assign(p + 1, p + len);
    pos_ += static_cast<size_t>(4) + len;
    if (pos_ > 65536) { buf_.erase(buf_.begin(), buf_.begin() + pos_); pos_ = 0; }
    return true;
  }
}

std::vector<unsigned char> encode_hello(uint32_t version, uint32_t w, uint32_t h, uint32_t d) {
  std::vector<unsigned char> b; put_u32(b, version); put_u32(b, w); put_u32(b, h); put_u32(b, d);
  return b;
}
bool decode_hello(const std::vector<unsigned char>& b, uint32_t& version,
                  uint32_t& w, uint32_t& h, uint32_t& d) {
  if (b.size() != 16) return false;
  version = get_u32(b.data()); w = get_u32(b.data() + 4);
  h = get_u32(b.data() + 8); d = get_u32(b.data() + 12);
  return true;
}

std::vector<unsigned char> encode_config(uint32_t w, uint32_t h, uint32_t fps,
                                         const std::vector<unsigned char>& ed) {
  std::vector<unsigned char> b;
  put_u32(b, w); put_u32(b, h); put_u32(b, fps);
  put_u32(b, static_cast<uint32_t>(ed.size()));
  b.insert(b.end(), ed.begin(), ed.end());
  return b;
}
bool decode_config(const std::vector<unsigned char>& b,
                   uint32_t& w, uint32_t& h, uint32_t& fps,
                   std::vector<unsigned char>& ed) {
  if (b.size() < 16) return false;
  w = get_u32(b.data()); h = get_u32(b.data() + 4); fps = get_u32(b.data() + 8);
  uint32_t n = get_u32(b.data() + 12);
  if (b.size() != static_cast<size_t>(16) + n) return false;
  ed.assign(b.begin() + 16, b.end());
  return true;
}

std::vector<unsigned char> encode_video(uint64_t pts_us, bool key,
                                        const std::vector<unsigned char>& nal) {
  std::vector<unsigned char> b;
  put_u64(b, pts_us);
  b.push_back(key ? 1 : 0);
  b.insert(b.end(), nal.begin(), nal.end());
  return b;
}
bool decode_video(const std::vector<unsigned char>& b,
                  uint64_t& pts_us, bool& key, std::vector<unsigned char>& nal) {
  if (b.size() < 9) return false;
  pts_us = get_u64(b.data());
  key = b[8] != 0;
  nal.assign(b.begin() + 9, b.end());
  return true;
}

std::vector<unsigned char> encode_input(uint8_t action, uint16_t x_norm, uint16_t y_norm) {
  std::vector<unsigned char> b;
  b.push_back(action);
  put_u16(b, x_norm); put_u16(b, y_norm);
  return b;
}
bool decode_input(const std::vector<unsigned char>& b,
                  uint8_t& action, uint16_t& x_norm, uint16_t& y_norm) {
  if (b.size() != 5) return false;
  action = b[0];
  x_norm = get_u16(b.data() + 1);
  y_norm = get_u16(b.data() + 3);
  return true;
}

}  // namespace droppix
