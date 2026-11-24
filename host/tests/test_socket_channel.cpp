#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include "socket_channel.h"

using namespace droppix;

TEST(SocketChannel, SendRecvOverSocketpair) {
  int sv[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  SocketChannel a(sv[0], nullptr);   // owns sv[0]

  const unsigned char msg[] = {1, 2, 3, 4, 5};
  ASSERT_TRUE(a.send_all(msg, sizeof(msg)));
  unsigned char got[5] = {0};
  ASSERT_EQ(::read(sv[1], got, 5), 5);
  EXPECT_EQ(0, memcmp(msg, got, 5));

  const unsigned char back[] = {9, 8, 7};
  ASSERT_EQ(::write(sv[1], back, 3), 3);
  EXPECT_TRUE(a.wait_readable(500));
  unsigned char rb[3] = {0};
  EXPECT_EQ(a.recv(rb, 3), 3);
  EXPECT_EQ(0, memcmp(back, rb, 3));

  ::close(sv[1]);
}

TEST(SocketChannel, WaitReadableTimesOutThenCloseDisconnects) {
  int sv[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  SocketChannel a(sv[0], nullptr);
  EXPECT_FALSE(a.wait_readable(50));   // nothing written
  EXPECT_TRUE(a.connected());
  a.close();
  EXPECT_FALSE(a.connected());
  ::close(sv[1]);
}

#include <arpa/inet.h>
#include <netinet/tcp.h>

TEST(SocketChannel, DeadPeerDetectionIsArmedOnTcp) {
  int ls = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(ls, 0);
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(bind(ls, (sockaddr*)&a, sizeof a), 0);
  ASSERT_EQ(listen(ls, 1), 0);
  socklen_t al = sizeof a;
  getsockname(ls, (sockaddr*)&a, &al);
  int c = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_EQ(connect(c, (sockaddr*)&a, sizeof a), 0);
  int fd = accept(ls, nullptr, nullptr);
  ASSERT_GE(fd, 0);

  enable_dead_peer_detection(fd);

  auto opt = [&](int level, int name) { int v = -1; socklen_t l = sizeof v; getsockopt(fd, level, name, &v, &l); return v; };
  EXPECT_EQ(opt(SOL_SOCKET, SO_KEEPALIVE), 1);
  EXPECT_EQ(opt(IPPROTO_TCP, TCP_KEEPIDLE), kKeepIdleS);
  EXPECT_EQ(opt(IPPROTO_TCP, TCP_KEEPINTVL), kKeepIntvlS);
  EXPECT_EQ(opt(IPPROTO_TCP, TCP_KEEPCNT), kKeepCnt);
  EXPECT_EQ(opt(IPPROTO_TCP, TCP_USER_TIMEOUT), kUserTimeoutMs);
  close(fd); close(c); close(ls);
}
