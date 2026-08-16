#include <gtest/gtest.h>

#include <vector>

#include "capturer.h"
#include "protocol.h"
#include "software_encoder.h"

using namespace droppix;

namespace {

Frame solid_frame(int w, int h, unsigned char v) {
  Frame f;
  f.width = w;
  f.height = h;
  f.stride = w * 4;
  f.bgra.assign(static_cast<size_t>(w) * h * 4, v);
  f.valid = true;
  return f;
}

}  // namespace

// A client whose decoder loses sync discards everything until the next keyframe. With a
// 2-second GOP that is a 2-second freeze, so the recovery path must actually produce an
// IDR on demand rather than merely being wired up.
TEST(KeyframeRequest, ForcesAnIdrOnTheNextFrame) {
  SoftwareEncoder enc;
  ASSERT_TRUE(enc.open(320, 240, 30, 2000));

  // Frame 0 is always a keyframe; get past it so a later IDR is unambiguous.
  auto first = enc.encode(solid_frame(320, 240, 0x10), 0);
  ASSERT_FALSE(first.empty());
  EXPECT_TRUE(first[0].keyframe);

  // Steady state: subsequent frames are deltas (gop_size = fps*2 = 60 frames away).
  bool saw_delta = false;
  for (int i = 1; i <= 5; ++i) {
    auto pkts = enc.encode(solid_frame(320, 240, static_cast<unsigned char>(0x10 + i)),
                           i * 33333);
    for (const auto& p : pkts) {
      if (!p.keyframe) saw_delta = true;
    }
  }
  ASSERT_TRUE(saw_delta) << "expected delta frames well before the GOP boundary";

  // The client asks; the very next frame must be a keyframe.
  enc.request_keyframe();
  auto forced = enc.encode(solid_frame(320, 240, 0x99), 6 * 33333);
  ASSERT_FALSE(forced.empty());
  EXPECT_TRUE(forced[0].keyframe) << "request_keyframe() must yield an IDR immediately";
}

TEST(KeyframeRequest, IsOneShotNotSticky) {
  SoftwareEncoder enc;
  ASSERT_TRUE(enc.open(320, 240, 30, 2000));
  enc.encode(solid_frame(320, 240, 0x10), 0);

  enc.request_keyframe();
  auto forced = enc.encode(solid_frame(320, 240, 0x20), 33333);
  ASSERT_FALSE(forced.empty());
  EXPECT_TRUE(forced[0].keyframe);

  // Without a fresh request we must fall back to deltas — a stuck flag would turn the
  // stream into all-intra and multiply the bitrate.
  bool any_delta = false;
  for (int i = 2; i <= 6; ++i) {
    auto pkts = enc.encode(solid_frame(320, 240, static_cast<unsigned char>(0x20 + i)),
                           i * 33333);
    for (const auto& p : pkts) if (!p.keyframe) any_delta = true;
  }
  EXPECT_TRUE(any_delta) << "request_keyframe() must be one-shot";
}

TEST(KeyframeRequest, ProtocolTypeIsStableAndClientToHost) {
  // Locked: the web/Android clients encode this value directly.
  EXPECT_EQ(static_cast<uint8_t>(MsgType::KeyframeRequest), 16);
  // A body-less control message round-trips through the shared parser.
  auto msg = encode_message(MsgType::KeyframeRequest, {});
  MessageParser p;
  p.feed(msg.data(), msg.size());
  ParsedMessage m;
  ASSERT_TRUE(p.next(m));
  EXPECT_EQ(m.type, MsgType::KeyframeRequest);
  EXPECT_TRUE(m.body.empty());
}

// STREAM_PARAMS carries a live fps/bitrate/audio change. The Kotlin encoder writes the same
// bytes, so this vector is the contract between them.
TEST(StreamParams, RoundTripsAndIsStable) {
  EXPECT_EQ(static_cast<uint8_t>(MsgType::StreamParams), 17);
  auto body = encode_stream_params(60, 16000, 1);
  ASSERT_EQ(body.size(), 9u);
  // Big-endian, like the rest of the protocol.
  EXPECT_EQ(body[0], 0); EXPECT_EQ(body[1], 0); EXPECT_EQ(body[2], 0); EXPECT_EQ(body[3], 60);
  EXPECT_EQ(body[8], 1);

  uint32_t fps = 0, kbps = 0; uint8_t aud = 0;
  ASSERT_TRUE(decode_stream_params(body, fps, kbps, aud));
  EXPECT_EQ(fps, 60u);
  EXPECT_EQ(kbps, 16000u);
  EXPECT_EQ(aud, 1);
}

TEST(StreamParams, RejectsAShortBody) {
  uint32_t fps = 0, kbps = 0; uint8_t aud = 0;
  EXPECT_FALSE(decode_stream_params(std::vector<unsigned char>(8, 0), fps, kbps, aud));
}
