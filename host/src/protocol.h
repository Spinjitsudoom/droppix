#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace droppix {

enum class MsgType : uint8_t {
  Hello = 1, Config = 2, Video = 3, Ping = 4, Pong = 5, Bye = 6, Input = 7,
  Orientation = 8, Audio = 9
};

// Protocol version sent in HELLO. Bump on any wire-format change.
constexpr uint32_t kProtocolVersion = 3;

// NOTE: the encoder uses x264 repeat-headers, so SPS/PPS travel IN-BAND ahead of
// each IDR. The CONFIG message's extradata is therefore typically empty and a
// decoder (e.g. Android MediaCodec) must configure from the in-band IDR headers.

// Wire frame: [u32 big-endian length][payload]; length covers payload;
// payload[0] = type byte, payload[1..] = body.
std::vector<unsigned char> encode_message(MsgType type,
                                          const std::vector<unsigned char>& body);

struct ParsedMessage {
  MsgType type;
  std::vector<unsigned char> body;
};

// Incremental parser: feed arbitrary byte chunks, pull complete messages.
class MessageParser {
 public:
  void feed(const unsigned char* data, size_t n);
  bool next(ParsedMessage& out);  // true if a complete message was dequeued
 private:
  std::vector<unsigned char> buf_;
  size_t pos_ = 0;  // consumed prefix
};

// Payload codecs (all integers big-endian).
// HELLO v3: u32 version, u32 width, u32 height, u32 density, then u16-prefixed
// name and id strings. decode_hello stays back-compatible with a bare 16-byte
// v2 body (no name/id), yielding empty name/id in that case.
std::vector<unsigned char> encode_hello(uint32_t version, uint32_t width,
                                        uint32_t height, uint32_t density,
                                        const std::string& name, const std::string& id);
bool decode_hello(const std::vector<unsigned char>& body, uint32_t& version,
                  uint32_t& width, uint32_t& height, uint32_t& density,
                  std::string& name, std::string& id);

std::vector<unsigned char> encode_config(uint32_t width, uint32_t height,
                                         uint32_t fps,
                                         const std::vector<unsigned char>& extradata);
bool decode_config(const std::vector<unsigned char>& body,
                   uint32_t& width, uint32_t& height, uint32_t& fps,
                   std::vector<unsigned char>& extradata);

std::vector<unsigned char> encode_video(uint64_t pts_us, bool keyframe,
                                        const std::vector<unsigned char>& nal);
bool decode_video(const std::vector<unsigned char>& body,
                  uint64_t& pts_us, bool& keyframe,
                  std::vector<unsigned char>& nal);

// INPUT (app->host): u8 action (0=down,1=move,2=up), u16 x_norm, u16 y_norm.
std::vector<unsigned char> encode_input(uint8_t action, uint16_t x_norm, uint16_t y_norm);
bool decode_input(const std::vector<unsigned char>& body,
                  uint8_t& action, uint16_t& x_norm, uint16_t& y_norm);

// ORIENTATION (app->host): u8 code (0=0°, 1=90°, 2=180°, 3=270°).
std::vector<unsigned char> encode_orientation(uint8_t code);
bool decode_orientation(const std::vector<unsigned char>& body, uint8_t& code);

}  // namespace droppix
