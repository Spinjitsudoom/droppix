#include <gtest/gtest.h>
#include <thread>
#include <arpa/inet.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include "transport_server.h"
#include "byte_channel.h"
#include "protocol.h"

using namespace droppix;

namespace {
// In-memory ByteChannel: preload bytes recv() hands out; capture bytes send_all() writes.
class FakeChannel : public droppix::ByteChannel {
 public:
  std::vector<unsigned char> to_recv;   // bytes recv() returns, FIFO
  std::vector<unsigned char> sent;      // bytes send_all() captured
  size_t rpos = 0;
  ssize_t recv(void* buf, size_t n) override {
    size_t avail = to_recv.size() - rpos;
    if (avail == 0) return 0;
    size_t k = std::min(n, avail);
    std::memcpy(buf, to_recv.data() + rpos, k);
    rpos += k;
    return static_cast<ssize_t>(k);
  }
  bool send_all(const unsigned char* p, size_t n) override {
    sent.insert(sent.end(), p, p + n);
    return true;
  }
  bool wait_readable(int) override { return rpos < to_recv.size(); }
  bool connected() const override { return true; }
  void close() override {}
};
}  // namespace

TEST(TransportServer, FramingOverFakeChannel) {
  TransportServer s;
  auto fake = std::make_unique<FakeChannel>();
  FakeChannel* fp = fake.get();
  // preload a HELLO for read_hello to parse
  fp->to_recv = encode_message(
      MsgType::Hello, encode_hello(kProtocolVersion, 800, 600, 200, "Fake", "fid"));
  s.adopt_channel(std::move(fake), "test");

  uint32_t ver, w, h, d, fps, bitrate; uint8_t audio, orient; uint16_t wcol, wrow;
  std::string name, id;
  ASSERT_TRUE(s.read_hello(ver, w, h, d, fps, audio, orient, bitrate, wcol, wrow, name, id, 0));
  EXPECT_EQ(ver, kProtocolVersion);
  EXPECT_EQ(w, 800u); EXPECT_EQ(h, 600u);
  EXPECT_EQ(name, "Fake"); EXPECT_EQ(id, "fid");

  // send_video must push framed bytes into the fake's sent buffer, decodable back.
  ASSERT_TRUE(s.send_video(42, true, {0x65}));
  MessageParser p; ParsedMessage m;
  p.feed(fp->sent.data(), fp->sent.size());
  ASSERT_TRUE(p.next(m));
  EXPECT_EQ(m.type, MsgType::Video);
}

TEST(TransportServer, ReadHelloV4Fields) {
  TransportServer s;
  auto fake = std::make_unique<FakeChannel>();
  fake->to_recv = encode_message(
      MsgType::Hello, encode_hello(4, 1600, 900, 120, "n", "i", 60, 1, 1));
  s.adopt_channel(std::move(fake), "test");

  uint32_t ver, w, h, d, fps, bitrate; uint8_t audio, orient; uint16_t wcol, wrow;
  std::string name, id;
  ASSERT_TRUE(s.read_hello(ver, w, h, d, fps, audio, orient, bitrate, wcol, wrow, name, id, 1000));
  EXPECT_EQ(fps, 60u); EXPECT_EQ(audio, 1); EXPECT_EQ(orient, 1); EXPECT_EQ(w, 1600u);
  EXPECT_EQ(bitrate, 0u);  // v4 body carries no bitrate field
}

TEST(TransportServer, ReadHelloV5Bitrate) {
  TransportServer s;
  auto fake = std::make_unique<FakeChannel>();
  fake->to_recv = encode_message(
      MsgType::Hello, encode_hello(5, 1280, 720, 160, "n", "i", 30, 1, 1, 9000));
  s.adopt_channel(std::move(fake), "test");

  uint32_t ver, w, h, d, fps, bitrate; uint8_t audio, orient; uint16_t wcol, wrow;
  std::string name, id;
  ASSERT_TRUE(s.read_hello(ver, w, h, d, fps, audio, orient, bitrate, wcol, wrow, name, id, 1000));
  EXPECT_EQ(fps, 30u); EXPECT_EQ(audio, 1); EXPECT_EQ(orient, 1);
  EXPECT_EQ(bitrate, 9000u);
}

// Minimal in-test client: connect, send HELLO, read one CONFIG + one VIDEO.
static void client_thread(uint16_t port, bool* ok) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) != 0) { ::close(fd); return; }

  auto hello = encode_message(MsgType::Hello, encode_hello(kProtocolVersion, 1920, 1080, 320, "TestTablet", "test-id-1"));
  ::send(fd, hello.data(), hello.size(), 0);

  MessageParser p; ParsedMessage m;
  unsigned char buf[4096];
  bool gotConfig = false, gotVideo = false;
  for (int i = 0; i < 50 && !(gotConfig && gotVideo); ++i) {
    ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) break;
    p.feed(buf, n);
    while (p.next(m)) {
      if (m.type == MsgType::Config) gotConfig = true;
      if (m.type == MsgType::Video) gotVideo = true;
    }
  }
  *ok = gotConfig && gotVideo;
  ::close(fd);
}

TEST(TransportServer, HandshakeThenVideo) {
  TransportServer s;
  ASSERT_TRUE(s.listen(0));      // ephemeral port
  uint16_t port = s.port();
  ASSERT_NE(port, 0);

  bool client_ok = false;
  std::thread t(client_thread, port, &client_ok);

  ASSERT_TRUE(s.accept_client(2000));
  uint32_t ver, w, h, d, fps, bitrate; uint8_t audio, orient; uint16_t wcol, wrow;
  std::string name, id;
  ASSERT_TRUE(s.read_hello(ver, w, h, d, fps, audio, orient, bitrate, wcol, wrow, name, id, 2000));
  EXPECT_EQ(ver, kProtocolVersion);
  EXPECT_EQ(w, 1920u); EXPECT_EQ(h, 1080u);
  EXPECT_EQ(name, "TestTablet"); EXPECT_EQ(id, "test-id-1");
  ASSERT_TRUE(s.send_config(1920, 1080, 30, {0x67, 0x42}));
  ASSERT_TRUE(s.send_video(1000, true, {0x00, 0x00, 0x00, 0x01, 0x65}));

  t.join();
  EXPECT_TRUE(client_ok);
}

TEST(TransportServer, ScrollHandlerFires) {
  TransportServer s;
  auto fake = std::make_unique<FakeChannel>();
  fake->to_recv = encode_message(MsgType::Scroll, encode_scroll(-2, 4, 500, 600));
  s.adopt_channel(std::move(fake), "test");

  int16_t gdx = 0, gdy = 0; uint16_t gx = 0, gy = 0; bool fired = false;
  s.set_scroll_handler([&](int16_t dx, int16_t dy, uint16_t x, uint16_t y) {
    gdx = dx; gdy = dy; gx = x; gy = y; fired = true;
  });

  s.poll_control();

  EXPECT_TRUE(fired);
  EXPECT_EQ(gdx, -2);
  EXPECT_EQ(gdy, 4);
  EXPECT_EQ(gx, 500u);
  EXPECT_EQ(gy, 600u);
}

TEST(TransportServer, KeyHandlerFires) {
  TransportServer s;
  auto fake = std::make_unique<FakeChannel>();
  fake->to_recv = encode_message(MsgType::Key, encode_key(30, 1));  // KEY_A down
  s.adopt_channel(std::move(fake), "test");

  uint16_t gotKc = 0; uint8_t gotAction = 0; int calls = 0;
  s.set_key_handler([&](uint16_t kc, uint8_t a) {
    gotKc = kc; gotAction = a; ++calls;
  });

  s.poll_control();

  EXPECT_EQ(calls, 1);
  EXPECT_EQ(gotKc, 30);
  EXPECT_EQ(gotAction, 1);
}

TEST(TransportServer, PenHandlerFires) {
  TransportServer s;
  auto fake = std::make_unique<FakeChannel>();
  fake->to_recv = encode_message(MsgType::Pen, encode_pen(40000, 20000, 900, 0x01));
  s.adopt_channel(std::move(fake), "test");
  uint16_t gx=0, gy=0, gp=0; uint8_t gf=0; int calls=0;
  s.set_pen_handler([&](uint16_t x, uint16_t y, uint16_t p, uint8_t f){ gx=x; gy=y; gp=p; gf=f; ++calls; });
  s.poll_control();
  EXPECT_EQ(calls, 1); EXPECT_EQ(gx, 40000); EXPECT_EQ(gy, 20000); EXPECT_EQ(gp, 900); EXPECT_EQ(gf, 0x01);
}

TEST(TransportServer, RelistenClosesOldSocketAndSucceeds) {
  TransportServer s;
  ASSERT_TRUE(s.listen(0));        // ephemeral
  ASSERT_TRUE(s.listen(0));        // re-listen must close the old fd, not fail/leak
  EXPECT_NE(s.port(), 0);
}

// A deliberate Disconnect must be distinguishable from a link that merely dropped. The
// streamer keeps re-serving after a drop (the tablet is expected back), so without this the
// monitor would survive a Disconnect — and over USB the host would re-run the accessory
// handshake, relaunching the app the user just left.
TEST(TransportServer, ByeMarksADeliberateDisconnect) {
  TransportServer s;
  auto fake = std::make_unique<FakeChannel>();
  fake->to_recv = encode_message(MsgType::Bye, {});
  s.adopt_channel(std::move(fake), "test");

  EXPECT_FALSE(s.said_bye());
  s.poll_control();
  EXPECT_TRUE(s.said_bye());
  EXPECT_FALSE(s.connected()) << "BYE must close the channel, not just flag it";
}

// A dropped socket is NOT a Bye: the streamer must keep waiting for the client to return.
TEST(TransportServer, DroppedLinkIsNotABye) {
  TransportServer s;
  auto fake = std::make_unique<FakeChannel>();
  fake->to_recv = {};            // recv() returns 0 => peer went away
  s.adopt_channel(std::move(fake), "test");

  s.poll_control();
  EXPECT_FALSE(s.said_bye()) << "an ordinary disconnect must not be treated as Disconnect";
}

// Messages already parsed ahead of a BYE still dispatch; anything after it is irrelevant
// because the channel is gone. Guards against the early return skipping real input.
TEST(TransportServer, MessagesBeforeByeStillDispatch) {
  TransportServer s;
  auto fake = std::make_unique<FakeChannel>();
  auto scroll = encode_message(MsgType::Scroll, encode_scroll(1, 2, 3, 4));
  auto bye = encode_message(MsgType::Bye, {});
  scroll.insert(scroll.end(), bye.begin(), bye.end());
  fake->to_recv = scroll;
  s.adopt_channel(std::move(fake), "test");

  bool scrolled = false;
  s.set_scroll_handler([&](int16_t, int16_t, uint16_t, uint16_t) { scrolled = true; });
  s.poll_control();

  EXPECT_TRUE(scrolled);
  EXPECT_TRUE(s.said_bye());
}
