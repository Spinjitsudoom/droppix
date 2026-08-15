#pragma once
#include <cstdint>
#include <vector>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace droppix {

// Encodes horizontal stripes of a BGRA frame to baseline JPEG for the spacedesk-compatible
// server: the spacedesk viewer expects each screen region as a standalone JFIF image
// (verified against a real server — 4 stripes of 2340x270 tiling a 2340x1080 display).
//
// Kept separate from the H.264 Encoder interface: that one is a temporal codec with
// reference frames, whereas every stripe here must decode standalone.
class JpegStripeEncoder {
 public:
  ~JpegStripeEncoder();

  // width/height describe ONE stripe. quality is 1..100 (mapped to the codec's qscale).
  bool open(int width, int height, int quality = 75);

  // Encode `rows` rows starting at `y_offset` of a BGRA buffer. Returns the JPEG bytes,
  // or empty on failure. The stripe geometry must match open().
  std::vector<unsigned char> encode(const unsigned char* bgra, int stride,
                                    int y_offset, int rows);

  int width() const { return width_; }
  int height() const { return height_; }

 private:
  void close();

  AVCodecContext* ctx_ = nullptr;
  AVFrame* frame_ = nullptr;
  AVPacket* pkt_ = nullptr;
  SwsContext* sws_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  // Monotonic: the encoder rejects a repeated pts, which would silently drop every
  // stripe after the first ("Invalid pts (0) <= last (0)").
  int64_t pts_ = 0;
};

}  // namespace droppix
