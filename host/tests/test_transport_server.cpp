#include <gtest/gtest.h>
#include <thread>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include "transport_server.h"
#include "protocol.h"

using namespace droppix;

// Minimal in-test client: connect, send HELLO, read one CONFIG + one VIDEO.
static void client_thread(uint16_t port, bool* ok) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) != 0) { ::close(fd); return; }

  auto hello = encode_message(MsgType::Hello, encode_hello(kProtocolVersion, 1920, 1080, 320));
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
  uint32_t ver, w, h, d;
  ASSERT_TRUE(s.read_hello(ver, w, h, d, 2000));
  EXPECT_EQ(ver, kProtocolVersion);
  EXPECT_EQ(w, 1920u); EXPECT_EQ(h, 1080u);
  ASSERT_TRUE(s.send_config(1920, 1080, 30, {0x67, 0x42}));
  ASSERT_TRUE(s.send_video(1000, true, {0x00, 0x00, 0x00, 0x01, 0x65}));

  t.join();
  EXPECT_TRUE(client_ok);
}

TEST(TransportServer, RelistenClosesOldSocketAndSucceeds) {
  TransportServer s;
  ASSERT_TRUE(s.listen(0));        // ephemeral
  ASSERT_TRUE(s.listen(0));        // re-listen must close the old fd, not fail/leak
  EXPECT_NE(s.port(), 0);
}
