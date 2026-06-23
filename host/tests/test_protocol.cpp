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
  auto body = encode_hello(1920, 1080, 320);
  uint32_t w, h, d;
  ASSERT_TRUE(decode_hello(body, w, h, d));
  EXPECT_EQ(w, 1920u); EXPECT_EQ(h, 1080u); EXPECT_EQ(d, 320u);
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
