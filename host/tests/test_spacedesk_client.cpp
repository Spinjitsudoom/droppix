#include <gtest/gtest.h>

#include <vector>

#include "spacedesk_client.h"
#include "spacedesk_protocol.h"

using namespace droppix::spacedesk;

namespace {

std::vector<ServerMessage> collect(ClientParser& p, const std::vector<unsigned char>& in) {
  std::vector<ServerMessage> out;
  p.feed(in.data(), in.size(), [&](const ServerMessage& m) { out.push_back(m); });
  return out;
}

// A server frame as the real server builds it: header describing the stripe + a JPEG.
std::vector<unsigned char> server_frame(uint32_t w, uint32_t h, uint32_t y0, uint32_t y1,
                                        const std::vector<unsigned char>& jpg) {
  auto m = build_frame_header(w, h, y0, y1, static_cast<uint32_t>(jpg.size()));
  m.insert(m.end(), jpg.begin(), jpg.end());
  return m;
}

}  // namespace

TEST(SpacedeskClient, HelloMirrorsTheRealViewersShape) {
  auto m = build_client_hello(1920, 1080, "{abc}", "droppix");
  Header h;
  ASSERT_TRUE(parse_header(m.data(), m.size(), h));
  EXPECT_EQ(h.type, kMsgHello);
  EXPECT_EQ(h.payload_len, m.size() - kHeaderSize);
  EXPECT_EQ(get_u32(m.data() + 52), 1920u);
  EXPECT_EQ(get_u32(m.data() + 88), 1080u);

  // A real server must be able to read our size back with its own parser.
  ClientHello parsed;
  ASSERT_TRUE(parse_client_hello(m.data(), m.size(), parsed));
  EXPECT_EQ(parsed.width, 1920u);
  EXPECT_EQ(parsed.height, 1080u);

  // Payload is UTF-16LE: ASCII byte then a zero high byte.
  EXPECT_EQ(m[kHeaderSize + 0], '{');
  EXPECT_EQ(m[kHeaderSize + 1], 0);
}

TEST(SpacedeskClient, HelloPayloadIsFixedSizeLikeTheRealViewers) {
  // A real server SILENTLY IGNORES a hello whose payload is merely the strings: it
  // answers nothing at all. The payload must be the fixed 334-byte block (total 462),
  // with the GUID at 0 and the model at 78, zero-padded. This was found by connecting
  // droppix's own client to a real spacedesk server, and it is the difference between
  // "no usable video" and a working session.
  auto m = build_client_hello(1920, 1080, "{9f09a77b-5128-4240-acb7-c95efab243ff}",
                              "droppix");
  EXPECT_EQ(m.size(), 462u) << "must match the real viewer's hello size";
  EXPECT_EQ(get_u32(m.data() + 4), 334u);
  // GUID at payload 0, model at payload 78, both UTF-16LE.
  EXPECT_EQ(m[kHeaderSize + 0], '{');
  EXPECT_EQ(m[kHeaderSize + 1], 0);
  EXPECT_EQ(m[kHeaderSize + 78], 'd');
  EXPECT_EQ(m[kHeaderSize + 79], 0);
  // Header constants the real viewer sets, verbatim.
  EXPECT_EQ(get_u32(m.data() + 36), 65541u);
  EXPECT_EQ(get_u32(m.data() + 44), 262204u);
  EXPECT_EQ(get_u32(m.data() + 124), 1u);
}

TEST(SpacedeskClient, OverlongIdentityCannotOverrunTheHelloPayload) {
  auto m = build_client_hello(800, 600, std::string(500, 'g'), std::string(500, 'm'));
  EXPECT_EQ(m.size(), 462u);
  EXPECT_EQ(get_u32(m.data() + 52), 800u);   // geometry not corrupted by the strings
  EXPECT_EQ(get_u32(m.data() + 88), 600u);
}

TEST(SpacedeskClient, ParsesAFrameAndItsStripeGeometry) {
  ClientParser p;
  std::vector<unsigned char> jpg = {0xff, 0xd8, 0xff, 0x01, 0x02, 0xff, 0xd9};
  auto msgs = collect(p, server_frame(2340, 1080, 270, 540, jpg));
  ASSERT_EQ(msgs.size(), 1u);
  EXPECT_EQ(msgs[0].type, kMsgFrame);
  EXPECT_EQ(msgs[0].display_w, 2340u);
  EXPECT_EQ(msgs[0].display_h, 1080u);
  EXPECT_EQ(msgs[0].y_start, 270u);
  EXPECT_EQ(msgs[0].y_end, 540u);
  EXPECT_EQ(msgs[0].jpeg, jpg);
}

TEST(SpacedeskClient, ReassemblesMessagesSplitAcrossReads) {
  // TCP gives no message boundaries; a stripe routinely spans several recv() calls.
  ClientParser p;
  std::vector<unsigned char> jpg(500, 0x5a);
  jpg[0] = 0xff; jpg[1] = 0xd8;
  auto whole = server_frame(640, 480, 0, 120, jpg);

  std::vector<ServerMessage> got;
  auto sink = [&](const ServerMessage& m) { got.push_back(m); };
  // Feed one byte at a time: the parser must emit exactly once, at the end.
  for (size_t i = 0; i < whole.size(); ++i) {
    p.feed(whole.data() + i, 1, sink);
    if (i + 1 < whole.size()) ASSERT_TRUE(got.empty()) << "emitted early at byte " << i;
  }
  ASSERT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0].jpeg.size(), jpg.size());
}

TEST(SpacedeskClient, HandlesSeveralMessagesInOneRead) {
  ClientParser p;
  std::vector<unsigned char> jpg = {0xff, 0xd8, 0xff, 0xd9};
  std::vector<unsigned char> stream;
  auto ack = build_control(kMsgAck);
  stream.insert(stream.end(), ack.begin(), ack.end());
  for (int i = 0; i < kStripes; ++i) {
    uint32_t s, e;
    stripe_bounds(480, i, s, e);
    auto f = server_frame(640, 480, s, e, jpg);
    stream.insert(stream.end(), f.begin(), f.end());
  }
  auto msgs = collect(p, stream);
  ASSERT_EQ(msgs.size(), 5u);
  EXPECT_EQ(msgs[0].type, kMsgAck);
  uint32_t covered = 0;
  for (size_t i = 1; i < msgs.size(); ++i) {
    EXPECT_EQ(msgs[i].type, kMsgFrame);
    EXPECT_EQ(msgs[i].y_start, covered);
    covered = msgs[i].y_end;
  }
  EXPECT_EQ(covered, 480u) << "the four stripes must tile the display";
}

TEST(SpacedeskClient, ControlMessagesCarryNoJpeg) {
  ClientParser p;
  std::vector<unsigned char> stream;
  for (uint32_t t : {kMsgAck, kMsgDisplay, kMsgHeartbeat}) {
    auto c = build_control(t);
    stream.insert(stream.end(), c.begin(), c.end());
  }
  auto msgs = collect(p, stream);
  ASSERT_EQ(msgs.size(), 3u);
  for (const auto& m : msgs) EXPECT_TRUE(m.jpeg.empty());
}

TEST(SpacedeskClient, AbsurdPayloadLengthDoesNotAllocateWildly) {
  // A desync must not be turned into a multi-gigabyte allocation.
  ClientParser p;
  std::vector<unsigned char> bad(kHeaderSize, 0);
  put_u32(bad.data() + 0, kMsgFrame);
  put_u32(bad.data() + 4, 0xffffffffu);
  auto msgs = collect(p, bad);
  EXPECT_TRUE(msgs.empty());
  EXPECT_EQ(p.buffered(), 0u) << "parser should discard and resync, not hoard";
}

TEST(SpacedeskClient, KeepaliveMatchesTheViewersShape) {
  auto k = build_client_keepalive(7);
  ASSERT_EQ(k.size(), kHeaderSize);
  EXPECT_EQ(get_u32(k.data()), kMsgKeepalive);
  EXPECT_EQ(get_u32(k.data() + 4), 0u);
  EXPECT_EQ(get_u32(k.data() + 24), 7u);
}
