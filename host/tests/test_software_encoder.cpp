#include <gtest/gtest.h>
#include <algorithm>
#include <set>
#include "software_encoder.h"
#include "capturer.h"

using namespace droppix;

// Build a solid-color BGRA frame.
static Frame make_frame(int w, int h, unsigned char b, unsigned char g, unsigned char r) {
  Frame f;
  f.width = w; f.height = h; f.stride = w * 4; f.valid = true;
  f.bgra.resize(static_cast<size_t>(w) * h * 4);
  for (size_t i = 0; i + 3 < f.bgra.size(); i += 4) {
    f.bgra[i] = b; f.bgra[i+1] = g; f.bgra[i+2] = r; f.bgra[i+3] = 0xFF;
  }
  return f;
}

// Build a BGRA frame whose content varies by pixel position and frame index,
// so it is high-entropy / not trivially compressible (unlike a solid color).
static Frame make_detail_frame(int w, int h, int i) {
  Frame f;
  f.width = w; f.height = h; f.stride = w * 4; f.valid = true;
  f.bgra.resize(static_cast<size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      size_t off = (static_cast<size_t>(y) * w + x) * 4;
      unsigned char v = static_cast<unsigned char>((x * 7 + y * 13 + i * 29) & 0xFF);
      f.bgra[off]   = v;
      f.bgra[off+1] = static_cast<unsigned char>((v * 3) & 0xFF);
      f.bgra[off+2] = static_cast<unsigned char>((v * 5) & 0xFF);
      f.bgra[off+3] = 0xFF;
    }
  }
  return f;
}

static bool starts_with_annexb(const std::vector<unsigned char>& d) {
  return d.size() >= 4 &&
         ((d[0]==0 && d[1]==0 && d[2]==0 && d[3]==1) ||
          (d[0]==0 && d[1]==0 && d[2]==1));
}

TEST(SoftwareEncoder, OpensAndEmitsKeyframeAnnexB) {
  SoftwareEncoder enc;
  ASSERT_TRUE(enc.open(320, 240, 30, 1000));
  std::vector<EncodedPacket> packets;
  // Feed several frames; zerolatency should emit promptly.
  for (int i = 0; i < 5; ++i) {
    auto f = make_frame(320, 240, (i*30)&0xFF, 0x40, 0x80);
    auto out = enc.encode(f, i * 33333);
    packets.insert(packets.end(), out.begin(), out.end());
  }
  auto tail = enc.flush();
  packets.insert(packets.end(), tail.begin(), tail.end());

  ASSERT_FALSE(packets.empty());
  EXPECT_TRUE(packets[0].keyframe);             // first output is an IDR
  EXPECT_TRUE(starts_with_annexb(packets[0].data));
}

TEST(SoftwareEncoder, PacketsCarryCallerMicrosecondPts) {
  SoftwareEncoder enc;
  ASSERT_TRUE(enc.open(320, 240, 30, 1000));
  std::set<int64_t> submitted;
  std::vector<EncodedPacket> packets;
  for (int i = 0; i < 8; ++i) {
    int64_t pts = 1000000 + int64_t(i) * 33333;  // microsecond-scale, distinct
    submitted.insert(pts);
    auto f = make_frame(320, 240, (i * 20) & 0xFF, 0x40, 0x80);
    auto out = enc.encode(f, pts);
    packets.insert(packets.end(), out.begin(), out.end());
  }
  auto tail = enc.flush();
  packets.insert(packets.end(), tail.begin(), tail.end());
  ASSERT_FALSE(packets.empty());
  for (auto& p : packets) {
    EXPECT_GE(p.pts_us, 1000000) << "pts looks like a frame index, not microseconds";
    EXPECT_TRUE(submitted.count(p.pts_us) > 0) << "pts not one of the submitted values";
  }
}

TEST(SoftwareEncoder, RespectsBitrateNoHugeFrames) {
  SoftwareEncoder enc;
  const int fps = 30, kbps = 2000, w = 1280, h = 720;
  ASSERT_TRUE(enc.open(w, h, fps, kbps));
  size_t total = 0, maxFrame = 0; int n = 0;
  for (int i = 0; i < 90; ++i) {
    Frame f = make_detail_frame(w, h, i);     // changing, detailed content
    for (auto& p : enc.encode(f, i * 33333)) { total += p.data.size(); maxFrame = std::max(maxFrame, p.data.size()); ++n; }
  }
  for (auto& p : enc.flush()) { total += p.data.size(); maxFrame = std::max(maxFrame, p.data.size()); ++n; }
  ASSERT_GT(n, 0);
  // ~2000 kbps @ 30 fps for 90 frames (3s) -> ~750 KB total expected; allow 4x slack.
  double kbits = total * 8.0 / 1000.0;
  EXPECT_LT(kbits, 4.0 * kbps * (90.0 / fps)) << "encoder ignoring bitrate (total " << kbits << " kbit)";
  // No single frame should be a multi-hundred-KB spike under VBV.
  EXPECT_LT(maxFrame, 300u * 1024u) << "frame spike: " << maxFrame << " bytes";
}
