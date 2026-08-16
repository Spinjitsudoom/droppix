#include <gtest/gtest.h>

#include <vector>

#include "capturer.h"
#include "software_encoder.h"

using namespace droppix;

namespace {

Frame frame(int w, int h, unsigned char v) {
  Frame f;
  f.width = w;
  f.height = h;
  f.stride = w * 4;
  f.bgra.assign(static_cast<size_t>(w) * h * 4, v);
  f.valid = true;
  return f;
}

}  // namespace

// Live fps/bitrate changes reopen the encoder mid-session. Before reset(), open() allocated
// a fresh AVCodecContext over the previous one, leaking the codec, packet and (on vaapi) the
// hw frames on every change — so this is the property that makes the feature safe at all.
TEST(EncoderReconfigure, ReopenProducesAWorkingEncoder) {
  SoftwareEncoder enc;
  ASSERT_TRUE(enc.open(320, 240, 30, 2000));
  auto a = enc.encode(frame(320, 240, 0x20), 0);
  ASSERT_FALSE(a.empty());
  EXPECT_TRUE(a[0].keyframe) << "first frame of a session is an IDR";

  // Reconfigure to a different fps/bitrate, as a live quality change does.
  ASSERT_TRUE(enc.open(320, 240, 60, 8000));
  auto b = enc.encode(frame(320, 240, 0x40), 0);
  ASSERT_FALSE(b.empty()) << "encoder must work after being reopened";
  EXPECT_TRUE(b[0].keyframe) << "a reopened encoder starts a fresh GOP";
}

TEST(EncoderReconfigure, RepeatedReopensKeepWorking) {
  // A user dragging a quality slider reopens repeatedly; each must still encode.
  SoftwareEncoder enc;
  for (int i = 0; i < 6; ++i) {
    const int fps = (i % 2 == 0) ? 30 : 60;
    const int kbps = 1000 + i * 500;
    ASSERT_TRUE(enc.open(320, 240, fps, kbps)) << "open #" << i;
    auto pkts = enc.encode(frame(320, 240, static_cast<unsigned char>(0x10 * i)), 0);
    ASSERT_FALSE(pkts.empty()) << "encode after open #" << i;
  }
}

TEST(EncoderReconfigure, PtsRestartsCleanlyAfterReopen) {
  // encode() maps codec ticks back to caller microseconds via an internal table; reset()
  // clears it so a reopened encoder does not resolve new frames against stale entries.
  SoftwareEncoder enc;
  ASSERT_TRUE(enc.open(320, 240, 30, 2000));
  for (int i = 0; i < 3; ++i) enc.encode(frame(320, 240, 0x30), i * 33333);

  ASSERT_TRUE(enc.open(320, 240, 30, 2000));
  auto pkts = enc.encode(frame(320, 240, 0x50), 999999);
  ASSERT_FALSE(pkts.empty());
  EXPECT_EQ(pkts[0].pts_us, 999999) << "pts must resolve to the value just submitted";
}

TEST(EncoderReconfigure, ReopenAtADifferentSizeWorks) {
  // Not used by live quality changes (resolution needs a new display), but reopening must
  // not corrupt state if it ever happens.
  SoftwareEncoder enc;
  ASSERT_TRUE(enc.open(320, 240, 30, 2000));
  ASSERT_FALSE(enc.encode(frame(320, 240, 0x20), 0).empty());

  ASSERT_TRUE(enc.open(640, 480, 30, 2000));
  auto pkts = enc.encode(frame(640, 480, 0x60), 0);
  ASSERT_FALSE(pkts.empty());
}

TEST(EncoderReconfigure, ExtradataStaysQueryableAfterReopen) {
  SoftwareEncoder enc;
  ASSERT_TRUE(enc.open(320, 240, 30, 2000));
  ASSERT_TRUE(enc.open(320, 240, 60, 4000));
  // Must not crash or read freed memory; in-band SPS/PPS means this is often empty.
  (void)enc.extradata();
  SUCCEED();
}
