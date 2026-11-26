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

// On the real Nexus 10, the app's old reader thread stayed blocked on the USB accessory after
// the app paused (an idle screen sends nothing to wake it), holding the accessory open, so the
// resumed app could never reopen it. The host now sends a PING whenever it has been quiet for a
// while: a stale reader wakes, sees its session is over, and lets go.
static size_t count_pings(const std::vector<unsigned char>& bytes) {
  MessageParser p; p.feed(bytes.data(), bytes.size());
  ParsedMessage m; size_t n = 0;
  while (p.next(m)) if (m.type == MsgType::Ping) ++n;
  return n;
}
TEST(TransportServer, KeepaliveWhenQuiet) {
  TransportServer s;
  auto fake = std::make_unique<FakeChannel>();
  FakeChannel* fp = fake.get();
  s.adopt_channel(std::move(fake), "test");
  s.note_sent(9'000);                        // the connect / last send
  s.keepalive(9'500, 1000);                  // quiet only 0.5 s: no ping
  EXPECT_EQ(count_pings(fp->sent), 0u);
  s.keepalive(10'000, 1000);                 // quiet 1 s: ping
  EXPECT_EQ(count_pings(fp->sent), 1u);
  s.keepalive(10'500, 1000);                 // only 0.5 s since: no ping
  EXPECT_EQ(count_pings(fp->sent), 1u);
  s.keepalive(11'000, 1000);                 // quiet 1 s: ping
  EXPECT_EQ(count_pings(fp->sent), 2u);
}
TEST(TransportServer, OtherTrafficPostponesTheKeepalive) {
  TransportServer s;
  auto fake = std::make_unique<FakeChannel>();
  FakeChannel* fp = fake.get();
  s.adopt_channel(std::move(fake), "test");
  s.note_sent(9'000);
  s.keepalive(10'000, 1000);                 // ping
  s.note_sent(10'800);                       // a video frame went out
  s.keepalive(11'500, 1000);                 // 0.7 s after that frame: no ping
  EXPECT_EQ(count_pings(fp->sent), 1u);
}

// The same stale-reader trap while the host WAITS for a new client's HELLO (after Disconnect):
// nothing was sent during the 10 s wait, so the old reader never woke and the tablet could never
// reopen the accessory. The wait now pings once a second too.
namespace {
class SilentChannel : public droppix::ByteChannel {
 public:
  std::vector<unsigned char> sent;
  ssize_t recv(void*, size_t) override { return 0; }
  bool send_all(const unsigned char* p, size_t n) override { sent.insert(sent.end(), p, p + n); return true; }
  bool wait_readable(int ms) override {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms > 0 ? ms : 1));
    return false;
  }
  bool connected() const override { return true; }
  void close() override {}
};
}  // namespace
TEST(TransportServer, PingsWhileWaitingForHello) {
  TransportServer s;
  auto ch = std::make_unique<SilentChannel>();
  SilentChannel* cp = ch.get();
  s.adopt_channel(std::move(ch), "test");
  uint32_t ver, w, h, d, fps, bitrate; uint8_t audio, orient; uint16_t wcol, wrow;
  std::string name, id;
  EXPECT_FALSE(s.read_hello(ver, w, h, d, fps, audio, orient, bitrate, wcol, wrow, name, id, 2200));
  EXPECT_GE(count_pings(cp->sent), 2u);
}

// Over USB the tablet can't see a session end: the link stays up and its reader just waits (or,
// worse, takes the next session's keep-alive pings as its own). The host now says BYE whenever
// it ends a session itself, so the app reconnects with a fresh HELLO.
TEST(TransportServer, SendByeTellsTheClient) {
  TransportServer s;
  auto fake = std::make_unique<FakeChannel>();
  FakeChannel* fp = fake.get();
  s.adopt_channel(std::move(fake), "test");
  EXPECT_TRUE(s.send_bye());
  MessageParser p; p.feed(fp->sent.data(), fp->sent.size());
  ParsedMessage m;
  ASSERT_TRUE(p.next(m));
  EXPECT_EQ(m.type, MsgType::Bye);
}
TEST(TransportServer, EndSessionSaysByeUnlessTheClientDid) {
  {
    // end_session() destroys the channel, so capture what it sent in a buffer we own.
    struct Capture : FakeChannel {
      std::vector<unsigned char>* out;
      explicit Capture(std::vector<unsigned char>* o) : out(o) {}
      bool send_all(const unsigned char* p, size_t n) override { out->insert(out->end(), p, p + n); return true; }
    };
    std::vector<unsigned char> sent;
    TransportServer s;
    s.adopt_channel(std::make_unique<Capture>(&sent), "test");
    s.end_session();                                   // host-initiated: tell the client
    MessageParser p; p.feed(sent.data(), sent.size());
    ParsedMessage m; ASSERT_TRUE(p.next(m)); EXPECT_EQ(m.type, MsgType::Bye);
    EXPECT_FALSE(s.connected());
  }
  {
    struct Capture : FakeChannel {
      std::vector<unsigned char>* out;
      explicit Capture(std::vector<unsigned char>* o) : out(o) {}
      bool send_all(const unsigned char* p, size_t n) override { out->insert(out->end(), p, p + n); return true; }
    };
    std::vector<unsigned char> sent;
    auto ch = std::make_unique<Capture>(&sent);
    ch->to_recv = encode_message(MsgType::Bye, {});
    TransportServer s;
    s.adopt_channel(std::move(ch), "test");
    s.poll_control();                                  // the client said BYE first (closes)
    EXPECT_TRUE(s.said_bye());
    s.end_session();
    EXPECT_TRUE(sent.empty());                         // nothing more to say to it
  }
}

// Over USB the PC can't see the tablet app close and reopen the accessory: the app's new
// session just sends HELLO on the same link while the old one is still streaming. A HELLO is
// only ever a session's first message, so mid-session it means "start over with these
// settings" (a new resolution, say), and the next read_hello must hand it back.
namespace {
std::unique_ptr<FakeChannel> channel_with(std::vector<unsigned char> bytes) {
  auto f = std::make_unique<FakeChannel>();
  f->to_recv = std::move(bytes);
  return f;
}
std::vector<unsigned char> hello(uint32_t w, uint32_t h) {
  return encode_message(MsgType::Hello, encode_hello(kProtocolVersion, w, h, 200, "n", "i"));
}
}  // namespace

TEST(TransportServer, HelloMidSessionMeansTheClientStartedOver) {
  TransportServer s;
  auto fake = channel_with(hello(768, 480));
  FakeChannel* fp = fake.get();
  s.adopt_channel(std::move(fake), "usb");
  uint32_t ver, w, h, d, fps, bitrate; uint8_t audio, orient; uint16_t wcol, wrow;
  std::string name, id;
  ASSERT_TRUE(s.read_hello(ver, w, h, d, fps, audio, orient, bitrate, wcol, wrow, name, id, 0));
  EXPECT_FALSE(s.client_restarted());

  auto again = hello(2560, 1600);
  fp->to_recv.insert(fp->to_recv.end(), again.begin(), again.end());
  s.poll_control();
  EXPECT_TRUE(s.client_restarted());
  EXPECT_TRUE(s.connected());   // the link stays: the client is already on it

  ASSERT_TRUE(s.read_hello(ver, w, h, d, fps, audio, orient, bitrate, wcol, wrow, name, id, 0));
  EXPECT_EQ(w, 2560u); EXPECT_EQ(h, 1600u);
  EXPECT_FALSE(s.client_restarted());
}

TEST(TransportServer, MessagesAfterARestartHelloAreKept) {
  TransportServer s;
  auto bytes = hello(768, 480);
  auto again = hello(1728, 1080);
  bytes.insert(bytes.end(), again.begin(), again.end());
  auto ping = encode_message(MsgType::Ping, std::vector<unsigned char>(8, 7));
  bytes.insert(bytes.end(), ping.begin(), ping.end());
  auto fake = channel_with(bytes);
  FakeChannel* fp = fake.get();
  s.adopt_channel(std::move(fake), "usb");
  uint32_t ver, w, h, d, fps, bitrate; uint8_t audio, orient; uint16_t wcol, wrow;
  std::string name, id;
  ASSERT_TRUE(s.read_hello(ver, w, h, d, fps, audio, orient, bitrate, wcol, wrow, name, id, 0));
  s.poll_control();
  ASSERT_TRUE(s.client_restarted());
  ASSERT_TRUE(s.read_hello(ver, w, h, d, fps, audio, orient, bitrate, wcol, wrow, name, id, 0));
  EXPECT_EQ(w, 1728u);

  fp->sent.clear();
  s.poll_control();   // the PING that followed the HELLO belongs to the new session
  MessageParser p; ParsedMessage m;
  p.feed(fp->sent.data(), fp->sent.size());
  ASSERT_TRUE(p.next(m));
  EXPECT_EQ(m.type, MsgType::Pong);
}

TEST(TransportServer, ANewChannelForgetsARestartHello) {
  TransportServer s;
  auto bytes = hello(768, 480);
  auto again = hello(1728, 1080);
  bytes.insert(bytes.end(), again.begin(), again.end());
  s.adopt_channel(channel_with(bytes), "a");
  uint32_t ver, w, h, d, fps, bitrate; uint8_t audio, orient; uint16_t wcol, wrow;
  std::string name, id;
  ASSERT_TRUE(s.read_hello(ver, w, h, d, fps, audio, orient, bitrate, wcol, wrow, name, id, 0));
  s.poll_control();
  ASSERT_TRUE(s.client_restarted());
  s.adopt_channel(channel_with({}), "b");   // a different client must not inherit it
  EXPECT_FALSE(s.client_restarted());
}

TEST(TransportServer, ARestartedSessionStartsWithAFreshSilenceClock) {
  // Rebuilding the monitor for the new session takes seconds with nobody reading the link;
  // that quiet must not count against the new session, or it ends as soon as it starts.
  TransportServer s;
  auto bytes = hello(768, 480);
  auto ping = encode_message(MsgType::Ping, std::vector<unsigned char>(8, 7));
  bytes.insert(bytes.end(), ping.begin(), ping.end());   // arms the silence check
  auto again = hello(1728, 1080);
  bytes.insert(bytes.end(), again.begin(), again.end());
  s.adopt_channel(channel_with(bytes), "usb");
  uint32_t ver, w, h, d, fps, bitrate; uint8_t audio, orient; uint16_t wcol, wrow;
  std::string name, id;
  ASSERT_TRUE(s.read_hello(ver, w, h, d, fps, audio, orient, bitrate, wcol, wrow, name, id, 0));
  s.poll_control();
  ASSERT_TRUE(s.client_restarted());
  ASSERT_TRUE(s.peer_silent(0));   // armed: the client has pinged on this link
  ASSERT_TRUE(s.read_hello(ver, w, h, d, fps, audio, orient, bitrate, wcol, wrow, name, id, 0));
  EXPECT_FALSE(s.peer_silent(0));
}

// A live settings change carries video (fps, bitrate) and audio. Video waits for the next
// frame (an encoder reopen needs one to send), but audio must not: on a still desktop no
// frame may come for minutes, and "audio on" would do nothing until something moved.
namespace {
TransportServer* with_params(TransportServer& s, uint32_t fps, uint32_t kbps, uint8_t audio) {
  auto bytes = hello(768, 480);
  auto p = encode_message(MsgType::StreamParams, encode_stream_params(fps, kbps, audio));
  bytes.insert(bytes.end(), p.begin(), p.end());
  s.adopt_channel(channel_with(bytes), "usb");
  uint32_t ver, w, h, d, f, b; uint8_t a, o; uint16_t c, r; std::string n, i;
  EXPECT_TRUE(s.read_hello(ver, w, h, d, f, a, o, b, c, r, n, i, 0));
  s.poll_control();
  return &s;
}
}  // namespace

TEST(TransportServer, AudioChangeIsTakenWithoutWaitingForVideo) {
  TransportServer s;
  with_params(s, 30, 4000, 1);
  bool on = false;
  ASSERT_TRUE(s.take_audio_request(on));
  EXPECT_TRUE(on);
  EXPECT_FALSE(s.take_audio_request(on));   // consumed once

  TransportServer::VideoParamsRequest v{};
  ASSERT_TRUE(s.take_video_params(v));      // still there for the next frame
  EXPECT_EQ(v.fps, 30u); EXPECT_EQ(v.bitrate_kbps, 4000u);
  EXPECT_FALSE(s.take_video_params(v));
}

TEST(TransportServer, VideoChangeIsTakenWithoutConsumingAudio) {
  TransportServer s;
  with_params(s, 60, 8000, 0);
  TransportServer::VideoParamsRequest v{};
  ASSERT_TRUE(s.take_video_params(v));
  bool on = true;
  ASSERT_TRUE(s.take_audio_request(on));
  EXPECT_FALSE(on);
}

// A tablet asleep on the cable reads nothing, so the keepalive ping sent while waiting for
// HELLO times out and closes the channel. read_hello then dereferenced the channel it had just
// lost: the streamer segfaulted, the GUI restarted it into the same crash until it gave up, and
// the tablet never got a session back when it woke (found on the Nexus 10, 2026-11-26).
namespace {
class DeafChannel : public droppix::ByteChannel {
 public:
  ssize_t recv(void*, size_t) override { return 0; }
  bool send_all(const unsigned char*, size_t) override { return false; }   // write times out
  bool wait_readable(int ms) override {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms > 0 ? ms : 1));
    return false;
  }
  bool connected() const override { return true; }
  void close() override {}
};
}  // namespace

TEST(TransportServer, AFailedKeepaliveWhileWaitingForHelloIsNotACrash) {
  TransportServer s;
  s.adopt_channel(std::make_unique<DeafChannel>(), "usb");
  uint32_t ver, w, h, d, fps, bitrate; uint8_t audio, orient; uint16_t wcol, wrow;
  std::string name, id;
  s.note_sent(-10'000);   // due a keepalive straight away
  EXPECT_FALSE(s.read_hello(ver, w, h, d, fps, audio, orient, bitrate, wcol, wrow, name, id, 3000));
  EXPECT_FALSE(s.connected());
}
