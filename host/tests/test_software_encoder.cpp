#include <gtest/gtest.h>
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
