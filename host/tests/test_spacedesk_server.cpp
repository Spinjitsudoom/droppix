#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <thread>
#include <vector>

#include "spacedesk_protocol.h"
#include "spacedesk_server.h"
#include "test_pattern_source.h"

using namespace droppix;
using namespace droppix::spacedesk;

namespace {

// A port well away from the real 28252 so a running spacedesk server (or a live droppix)
// cannot make these tests flaky.
constexpr uint16_t kTestPort = 28952;

int connect_local(uint16_t port) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

bool read_exact(int fd, unsigned char* p, size_t n, int timeout_ms = 5000) {
  size_t got = 0;
  while (got < n) {
    pollfd pfd{fd, POLLIN, 0};
    if (::poll(&pfd, 1, timeout_ms) <= 0) return false;
    ssize_t r = ::recv(fd, p + got, n - got, 0);
    if (r <= 0) return false;
    got += static_cast<size_t>(r);
  }
  return true;
}

// The viewer's hello: type 0, screen size at @52/@88, as captured from a real device.
std::vector<unsigned char> viewer_hello(uint32_t w, uint32_t h) {
  std::vector<unsigned char> m(kHeaderSize, 0);
  put_u32(m.data() + 0, kMsgHello);
  put_u32(m.data() + 4, 0);
  put_u32(m.data() + 52, w);
  put_u32(m.data() + 88, h);
  return m;
}

SpacedeskServer::SourceFactory pattern_factory() {
  return [](int w, int h) -> std::unique_ptr<FrameSource> {
    return std::make_unique<TestPatternSource>(w, h, 60);
  };
}

}  // namespace

TEST(SpacedeskServer, StreamsJpegStripesToAViewer) {
  SpacedeskServer srv(pattern_factory(), "droppix-test");
  srv.set_port(kTestPort);
  ASSERT_TRUE(srv.start());

  int fd = connect_local(kTestPort);
  ASSERT_GE(fd, 0);

  auto hello = viewer_hello(640, 480);
  ASSERT_EQ(::send(fd, hello.data(), hello.size(), 0),
            static_cast<ssize_t>(hello.size()));

  // The viewer must receive the real server's full opening sequence before any frames:
  // ack, two display messages, two heartbeats. With only the ack it sits connected with
  // the display OFF, which is exactly the bug this asserts against.
  unsigned char hdr[kHeaderSize];
  Header h{};
  const uint32_t expect[] = {kMsgAck, kMsgDisplay, kMsgDisplay, kMsgHeartbeat, kMsgHeartbeat};
  for (size_t i = 0; i < 5; ++i) {
    ASSERT_TRUE(read_exact(fd, hdr, kHeaderSize)) << "opening message " << i;
    ASSERT_TRUE(parse_header(hdr, kHeaderSize, h));
    EXPECT_EQ(h.type, expect[i]) << "opening message " << i << " out of order";
    EXPECT_EQ(h.payload_len, 0u);
  }

  // Then a full set of stripes, each a real JPEG covering its slice of the display.
  uint32_t covered = 0;
  for (int i = 0; i < kStripes; ++i) {
    ASSERT_TRUE(read_exact(fd, hdr, kHeaderSize)) << "stripe " << i;
    ASSERT_TRUE(parse_header(hdr, kHeaderSize, h));
    ASSERT_EQ(h.type, kMsgFrame);
    ASSERT_GT(h.payload_len, 0u);

    EXPECT_EQ(get_u32(hdr + 8), 640u);        // full display width
    EXPECT_EQ(get_u32(hdr + 12), 480u);       // full display height
    EXPECT_EQ(get_u32(hdr + 16), 640u * 4);   // stride, 32bpp
    const uint32_t y0 = get_u32(hdr + 28);
    const uint32_t y1 = get_u32(hdr + 36);
    EXPECT_EQ(y0, covered) << "stripes must tile without gaps";
    EXPECT_GT(y1, y0);
    covered = y1;

    std::vector<unsigned char> jpg(h.payload_len);
    ASSERT_TRUE(read_exact(fd, jpg.data(), jpg.size()));
    // SOI/EOI: the viewer hands this straight to a JPEG decoder.
    EXPECT_EQ(jpg[0], 0xff);
    EXPECT_EQ(jpg[1], 0xd8);
    EXPECT_EQ(jpg[jpg.size() - 2], 0xff);
    EXPECT_EQ(jpg[jpg.size() - 1], 0xd9);
  }
  EXPECT_EQ(covered, 480u) << "one frame's stripes must cover the whole display";

  ::close(fd);
  srv.stop();
}

TEST(SpacedeskServer, AnswersTheViewerDiscoveryBroadcast) {
  SpacedeskServer srv(pattern_factory(), "droppix-disco");
  srv.set_port(kTestPort + 1);
  ASSERT_TRUE(srv.start());

  int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(fd, 0);
  timeval tv{2, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(kTestPort + 1);
  inet_pton(AF_INET, "127.0.0.1", &to.sin_addr);

  const char req[] = "SPACEDESK-NET-CLIENT";
  ASSERT_GT(::sendto(fd, req, sizeof(req), 0,
                     reinterpret_cast<sockaddr*>(&to), sizeof(to)), 0);

  unsigned char buf[1024];
  ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
  ASSERT_EQ(n, static_cast<ssize_t>(kDiscoveryResponseSize));
  EXPECT_EQ(buf[0], 'd');                       // name, UTF-16LE
  EXPECT_EQ(buf[1], 0);
  EXPECT_EQ(get_u32(buf + 260), kTestPort + 1u);  // the port we actually listen on

  ::close(fd);
  srv.stop();
}

TEST(SpacedeskServer, StartFailsCleanlyWhenThePortIsTaken) {
  SpacedeskServer a(pattern_factory(), "first");
  a.set_port(kTestPort + 2);
  ASSERT_TRUE(a.start());

  // A real spacedesk server (or a second droppix) owning the port must not crash or
  // silently half-start us — it must report failure and leave nothing running.
  SpacedeskServer b(pattern_factory(), "second");
  b.set_port(kTestPort + 2);
  EXPECT_FALSE(b.start());
  EXPECT_FALSE(b.running());

  a.stop();
}

TEST(SpacedeskServer, StopIsIdempotentAndSafeWithoutAClient) {
  SpacedeskServer srv(pattern_factory(), "droppix");
  srv.set_port(kTestPort + 3);
  ASSERT_TRUE(srv.start());
  EXPECT_TRUE(srv.running());
  srv.stop();
  EXPECT_FALSE(srv.running());
  srv.stop();   // must not hang or double-join
}

TEST(SpacedeskServer, IgnoresAFirstMessageThatIsNotAHello) {
  SpacedeskServer srv(pattern_factory(), "droppix");
  srv.set_port(kTestPort + 4);
  ASSERT_TRUE(srv.start());

  int fd = connect_local(kTestPort + 4);
  ASSERT_GE(fd, 0);
  std::vector<unsigned char> junk(kHeaderSize, 0);
  put_u32(junk.data(), kMsgKeepalive);   // a keepalive, not a hello
  ::send(fd, junk.data(), junk.size(), 0);

  // The server must drop the session rather than stream to a client it never handshook.
  unsigned char b[16];
  pollfd pfd{fd, POLLIN, 0};
  int r = ::poll(&pfd, 1, 1500);
  if (r > 0) EXPECT_LE(::recv(fd, b, sizeof(b), 0), 0);

  ::close(fd);
  srv.stop();
}
