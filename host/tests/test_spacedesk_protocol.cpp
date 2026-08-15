#include <gtest/gtest.h>

#include <cstring>

#include "spacedesk_protocol.h"

using namespace droppix::spacedesk;

// Bytes below are locked against a REAL spacedesk server + viewer capture
// (docs/superpowers/specs/2026-08-15-spacedesk-protocol-notes.md). If the viewer stops
// connecting, these vectors are what to re-verify first.

TEST(SpacedeskProtocol, RecognisesTheViewerDiscoveryMagic) {
  const unsigned char req[] = "SPACEDESK-NET-CLIENT";
  EXPECT_TRUE(is_discovery_request(req, sizeof(req)));       // with the trailing NUL
  EXPECT_TRUE(is_discovery_request(req, sizeof(req) - 1));   // without it
  const unsigned char other[] = "SPACEDESK-NET-SERVER";
  EXPECT_FALSE(is_discovery_request(other, sizeof(other)));
  EXPECT_FALSE(is_discovery_request(req, 4));
}

TEST(SpacedeskProtocol, DiscoveryResponseMatchesTheRealServerLayout) {
  auto r = build_discovery_response("SPINJIT-5OU42LU");
  ASSERT_EQ(r.size(), 308u);
  // Name is UTF-16LE at @0: ASCII byte then a zero high byte.
  EXPECT_EQ(r[0], 'S');
  EXPECT_EQ(r[1], 0);
  EXPECT_EQ(r[2], 'P');
  EXPECT_EQ(r[14], '-');
  // Port and the capability constants the real server reports.
  EXPECT_EQ(get_u32(r.data() + 260), 28252u);
  EXPECT_EQ(get_u32(r.data() + 264), 8u);
  EXPECT_EQ(get_u32(r.data() + 272), 4u);
  EXPECT_EQ(get_u32(r.data() + 276), 8u);
  EXPECT_EQ(get_u32(r.data() + 280), 43u);
}

TEST(SpacedeskProtocol, DiscoveryResponseTruncatesAnOverlongName) {
  auto r = build_discovery_response(std::string(400, 'A'));
  ASSERT_EQ(r.size(), 308u);
  EXPECT_EQ(get_u32(r.data() + 260), 28252u);   // port not overwritten by the name
}

TEST(SpacedeskProtocol, ParsesTheCapturedClientHello) {
  // Reconstructed from the real viewer's 462-byte hello: type 0 @0, payload 334 @4,
  // width 2340 @52, height 1080 @88 (a Redmi 9's panel).
  std::vector<unsigned char> hello(kHeaderSize + 334, 0);
  put_u32(hello.data() + 0, kMsgHello);
  put_u32(hello.data() + 4, 334);
  put_u32(hello.data() + 52, 2340);
  put_u32(hello.data() + 88, 1080);

  Header h;
  ASSERT_TRUE(parse_header(hello.data(), hello.size(), h));
  EXPECT_EQ(h.type, kMsgHello);
  EXPECT_EQ(h.payload_len, 334u);
  EXPECT_EQ(kHeaderSize + h.payload_len, 462u);   // the captured total

  ClientHello c;
  ASSERT_TRUE(parse_client_hello(hello.data(), hello.size(), c));
  EXPECT_EQ(c.width, 2340u);
  EXPECT_EQ(c.height, 1080u);
}

TEST(SpacedeskProtocol, RejectsShortOrWrongTypeHello) {
  std::vector<unsigned char> buf(kHeaderSize, 0);
  put_u32(buf.data(), kMsgKeepalive);           // not a hello
  ClientHello c;
  EXPECT_FALSE(parse_client_hello(buf.data(), buf.size(), c));
  Header h;
  EXPECT_FALSE(parse_header(buf.data(), 64, h));  // truncated header
}

TEST(SpacedeskProtocol, FrameHeaderMatchesTheRealServersFields) {
  const uint32_t jpeg_len = 18062;               // a size seen on the wire
  auto h = build_frame_header(2340, 1080, 270, 540, jpeg_len);
  ASSERT_EQ(h.size(), kHeaderSize);
  EXPECT_EQ(get_u32(h.data() + 0), kMsgFrame);
  EXPECT_EQ(get_u32(h.data() + 4), jpeg_len);
  EXPECT_EQ(get_u32(h.data() + 8), 2340u);       // full display width
  EXPECT_EQ(get_u32(h.data() + 12), 1080u);      // full display height
  EXPECT_EQ(get_u32(h.data() + 16), 2340u * 4);  // stride, 32bpp
  EXPECT_EQ(get_u32(h.data() + 28), 270u);       // stripe y start
  EXPECT_EQ(get_u32(h.data() + 32), 2340u);      // stripe x end
  EXPECT_EQ(get_u32(h.data() + 36), 540u);       // stripe y end
  EXPECT_EQ(get_u32(h.data() + 68), jpeg_len);   // length repeated
}

TEST(SpacedeskProtocol, StripesTileTheDisplayExactly) {
  const uint32_t h = 1080;
  uint32_t prev_end = 0;
  for (int i = 0; i < kStripes; ++i) {
    uint32_t s, e;
    stripe_bounds(h, i, s, e);
    EXPECT_EQ(s, prev_end) << "stripe " << i << " must start where the last ended";
    EXPECT_GT(e, s);
    prev_end = e;
  }
  EXPECT_EQ(prev_end, h) << "stripes must cover the whole display";
  // The captured session used exactly 4 x 270 rows.
  uint32_t s0, e0;
  stripe_bounds(h, 0, s0, e0);
  EXPECT_EQ(e0 - s0, 270u);
}

TEST(SpacedeskProtocol, StripesTileAHeightNotDivisibleByFour) {
  const uint32_t h = 1077;                        // remainder lands in the last stripe
  uint32_t prev_end = 0;
  for (int i = 0; i < kStripes; ++i) {
    uint32_t s, e;
    stripe_bounds(h, i, s, e);
    EXPECT_EQ(s, prev_end);
    EXPECT_GT(e, s);
    prev_end = e;
  }
  EXPECT_EQ(prev_end, h);
}

TEST(SpacedeskProtocol, SessionOpenMatchesTheRealServersSequence) {
  // Captured from a live spacedesk server, in order: ack, two display messages, two
  // heartbeats — and only then frames. A viewer that gets only the ack stays connected
  // with its display OFF, so this order is load-bearing, not cosmetic.
  auto seq = build_session_open();
  ASSERT_EQ(seq.size(), 5u);
  for (const auto& m : seq) {
    ASSERT_EQ(m.size(), kHeaderSize);
    EXPECT_EQ(get_u32(m.data() + 4), 0u) << "opening messages carry no payload";
  }
  EXPECT_EQ(get_u32(seq[0].data()), kMsgAck);
  EXPECT_EQ(get_u32(seq[1].data()), kMsgDisplay);
  EXPECT_EQ(get_u32(seq[1].data() + 8), 0u);
  EXPECT_EQ(get_u32(seq[1].data() + 12), 1u);
  EXPECT_EQ(get_u32(seq[2].data()), kMsgDisplay);
  EXPECT_EQ(get_u32(seq[2].data() + 8), 1u);
  EXPECT_EQ(get_u32(seq[2].data() + 12), 1u);
  EXPECT_EQ(get_u32(seq[3].data()), kMsgHeartbeat);
  EXPECT_EQ(get_u32(seq[4].data()), kMsgHeartbeat);
}

TEST(SpacedeskProtocol, HeartbeatMatchesTheRealServer) {
  auto hb = build_heartbeat();
  ASSERT_EQ(hb.size(), kHeaderSize);
  EXPECT_EQ(get_u32(hb.data()), kMsgHeartbeat);
  EXPECT_EQ(get_u32(hb.data() + 8), 15u);
  EXPECT_EQ(get_u32(hb.data() + 12), 1u);
}

TEST(SpacedeskProtocol, ControlMessagesAreBareHeaders) {
  for (uint32_t t : {kMsgAck, kMsgHeartbeat, kMsgDisplay}) {
    auto m = build_control(t);
    ASSERT_EQ(m.size(), kHeaderSize);
    EXPECT_EQ(get_u32(m.data()), t);
    EXPECT_EQ(get_u32(m.data() + 4), 0u);   // no payload
  }
}
