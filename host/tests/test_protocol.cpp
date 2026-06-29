#include <gtest/gtest.h>
#include "protocol.h"

using namespace droppix;

TEST(Protocol, EncodeMessageHasBigEndianLengthAndType) {
  auto m = encode_message(MsgType::Video, {0xAA, 0xBB});
  // length = 1 (type) + 2 (body) = 3
  ASSERT_EQ(m.size(), 4u + 3u);
  EXPECT_EQ(m[0], 0x00); EXPECT_EQ(m[1], 0x00);
  EXPECT_EQ(m[2], 0x00); EXPECT_EQ(m[3], 0x03);
  EXPECT_EQ(m[4], static_cast<unsigned char>(MsgType::Video));
  EXPECT_EQ(m[5], 0xAA); EXPECT_EQ(m[6], 0xBB);
}

TEST(Protocol, ParserReassemblesAcrossPartialFeeds) {
  auto m = encode_message(MsgType::Ping, {1, 2, 3});
  MessageParser p;
  // feed in two halves
  p.feed(m.data(), 3);
  ParsedMessage out;
  EXPECT_FALSE(p.next(out));         // incomplete
  p.feed(m.data() + 3, m.size() - 3);
  ASSERT_TRUE(p.next(out));
  EXPECT_EQ(out.type, MsgType::Ping);
  EXPECT_EQ(out.body, (std::vector<unsigned char>{1, 2, 3}));
  EXPECT_FALSE(p.next(out));         // nothing left
}

TEST(Protocol, ParserHandlesTwoBackToBackMessages) {
  auto a = encode_message(MsgType::Hello, {9});
  auto b = encode_message(MsgType::Bye, {});
  MessageParser p;
  p.feed(a.data(), a.size());
  p.feed(b.data(), b.size());
  ParsedMessage out;
  ASSERT_TRUE(p.next(out)); EXPECT_EQ(out.type, MsgType::Hello);
  ASSERT_TRUE(p.next(out)); EXPECT_EQ(out.type, MsgType::Bye);
  EXPECT_FALSE(p.next(out));
}

TEST(Protocol, HelloRoundTrip) {
  auto body = encode_hello(kProtocolVersion, 1920, 1080, 320, "Nexus 10", "abc123");
  uint32_t ver, w, h, d; std::string name, id;
  ASSERT_TRUE(decode_hello(body, ver, w, h, d, name, id));
  EXPECT_EQ(ver, kProtocolVersion);
  EXPECT_EQ(w, 1920u); EXPECT_EQ(h, 1080u); EXPECT_EQ(d, 320u);
  EXPECT_EQ(name, "Nexus 10"); EXPECT_EQ(id, "abc123");
}

TEST(Protocol, HelloV3RoundTrip) {
  auto body = encode_hello(3, 1920, 1080, 320, "Nexus 10", "abc123");
  uint32_t v,w,h,d; std::string name,id;
  ASSERT_TRUE(decode_hello(body, v,w,h,d, name,id));
  EXPECT_EQ(v,3u); EXPECT_EQ(w,1920u); EXPECT_EQ(h,1080u); EXPECT_EQ(d,320u);
  EXPECT_EQ(name,"Nexus 10"); EXPECT_EQ(id,"abc123");
}
TEST(Protocol, HelloV2BackCompatNoNameId) {
  // old 16-byte HELLO (version 2) -> name/id default empty, still decodes.
  std::vector<unsigned char> b;
  // reuse the v3 encoder but truncate to the first 16 bytes
  auto full = encode_hello(2, 1280, 720, 160, "x", "y");
  b.assign(full.begin(), full.begin()+16);
  uint32_t v,w,h,d; std::string name,id;
  ASSERT_TRUE(decode_hello(b, v,w,h,d, name,id));
  EXPECT_EQ(v,2u); EXPECT_EQ(w,1280u); EXPECT_TRUE(name.empty()); EXPECT_TRUE(id.empty());
}

TEST(Protocol, ConfigRoundTrip) {
  std::vector<unsigned char> extradata{0x67, 0x42, 0x00};
  auto body = encode_config(1920, 1080, 30, extradata);
  uint32_t w, h, fps; std::vector<unsigned char> ed;
  ASSERT_TRUE(decode_config(body, w, h, fps, ed));
  EXPECT_EQ(w, 1920u); EXPECT_EQ(h, 1080u); EXPECT_EQ(fps, 30u);
  EXPECT_EQ(ed, extradata);
}

TEST(Protocol, VideoRoundTrip) {
  std::vector<unsigned char> nal{0x00, 0x00, 0x00, 0x01, 0x65, 0x11};
  auto body = encode_video(123456, true, nal);
  uint64_t pts; bool key; std::vector<unsigned char> out;
  ASSERT_TRUE(decode_video(body, pts, key, out));
  EXPECT_EQ(pts, 123456u); EXPECT_TRUE(key); EXPECT_EQ(out, nal);
}

TEST(Protocol, ParserRejectsOversizedLengthWithoutOverread) {
  MessageParser p;
  // length word = 0xFFFFFFFF, then a couple stray bytes. Must not crash/overread.
  unsigned char bad[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0xAA};
  p.feed(bad, sizeof(bad));
  ParsedMessage out;
  EXPECT_FALSE(p.next(out));
}

TEST(Protocol, ParserResyncsPastManyZeroLengthWordsWithoutRecursion) {
  MessageParser p;
  std::vector<unsigned char> data;
  for (int i = 0; i < 5000; ++i) { data.insert(data.end(), {0, 0, 0, 0}); }  // len=0 words
  auto good = encode_message(MsgType::Ping, {7});
  data.insert(data.end(), good.begin(), good.end());
  p.feed(data.data(), data.size());
  ParsedMessage out;
  ASSERT_TRUE(p.next(out));     // must reach the real message via a loop, not blow the stack
  EXPECT_EQ(out.type, MsgType::Ping);
  EXPECT_EQ(out.body, (std::vector<unsigned char>{7}));
}

TEST(Protocol, InputRoundTrip) {
  auto body = encode_input(0, 30000, 40000);
  uint8_t a; uint16_t x, y;
  ASSERT_TRUE(decode_input(body, a, x, y));
  EXPECT_EQ(a, 0); EXPECT_EQ(x, 30000); EXPECT_EQ(y, 40000);
}
TEST(Protocol, InputWireLayout) {
  auto m = encode_message(MsgType::Input, encode_input(2, 0x0102, 0x0304));
  // len = 1(type)+5(body)=6; type=7; body = 02 0102 0304 (big-endian)
  ASSERT_EQ(m.size(), 4u + 6u);
  EXPECT_EQ(m[3], 6); EXPECT_EQ(m[4], 7);
  EXPECT_EQ(m[5], 0x02);
  EXPECT_EQ(m[6], 0x01); EXPECT_EQ(m[7], 0x02);
  EXPECT_EQ(m[8], 0x03); EXPECT_EQ(m[9], 0x04);
}
TEST(Protocol, InputTooShortInvalid) {
  uint8_t a; uint16_t x, y;
  EXPECT_FALSE(decode_input({0, 0}, a, x, y));
}

TEST(Protocol, OrientationRoundTrip) {
  for (uint8_t c : {uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3)}) {
    uint8_t out;
    ASSERT_TRUE(decode_orientation(encode_orientation(c), out));
    EXPECT_EQ(out, c);
  }
}
TEST(Protocol, OrientationWireLayout) {
  auto m = encode_message(MsgType::Orientation, encode_orientation(1));
  // len = 1(type)+1(body)=2; type=8; body = 01
  ASSERT_EQ(m.size(), 4u + 2u);
  EXPECT_EQ(m[3], 2); EXPECT_EQ(m[4], 8); EXPECT_EQ(m[5], 0x01);
}
TEST(Protocol, OrientationBadLengthInvalid) {
  uint8_t c;
  EXPECT_FALSE(decode_orientation({}, c));
  EXPECT_FALSE(decode_orientation({0, 0}, c));
}

TEST(Protocol, AudioMessageFraming) {
  // length = 1 (type) + 4 (body) = 5, big-endian; type = Audio(9).
  std::vector<unsigned char> pcm = {0xDE, 0xAD, 0xBE, 0xEF};
  auto m = droppix::encode_message(droppix::MsgType::Audio, pcm);
  std::vector<unsigned char> expected = {0,0,0,5, 9, 0xDE,0xAD,0xBE,0xEF};
  EXPECT_EQ(m, expected);
}
