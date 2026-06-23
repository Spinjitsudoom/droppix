#pragma once
#include <cstdint>
#include <vector>

namespace droppix {

enum class MsgType : uint8_t {
  Hello = 1, Config = 2, Video = 3, Ping = 4, Pong = 5, Bye = 6
};

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
std::vector<unsigned char> encode_hello(uint32_t width, uint32_t height,
                                        uint32_t density);
bool decode_hello(const std::vector<unsigned char>& body,
                  uint32_t& width, uint32_t& height, uint32_t& density);

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

}  // namespace droppix
