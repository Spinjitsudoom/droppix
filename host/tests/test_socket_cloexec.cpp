#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

#include "transport_server.h"

using namespace droppix;

// The streamer runs `parec` for audio capture via popen(), which forks and hands the child
// every descriptor that is not marked close-on-exec. Without CLOEXEC that includes the
// listening socket, so `parec` — which lives for the whole audio session and outlives the
// streamer if the streamer is killed rather than shut down — keeps the port bound. A
// restart then fails to bind a port whose owner is invisible to anyone looking for
// droppix_stream. SO_REUSEADDR does not help: another process genuinely holds the socket.
TEST(SocketCloexec, ListeningSocketIsNotInheritedByChildren) {
  TransportServer tx;
  ASSERT_TRUE(tx.listen(0));   // port 0 = let the kernel pick, so the test cannot collide

  const int fd = tx.listen_fd();
  ASSERT_GE(fd, 0);

  const int flags = ::fcntl(fd, F_GETFD);
  ASSERT_NE(flags, -1);
  EXPECT_TRUE(flags & FD_CLOEXEC)
      << "listening socket would survive into popen()'d children (parec, xrandr) "
         "and hold the port after the streamer is gone";
}

// listen() is documented as safe to call again; the replacement socket must be just as
// non-inheritable as the first, or the flag silently depends on call order.
TEST(SocketCloexec, RelistenKeepsTheFlag) {
  TransportServer tx;
  ASSERT_TRUE(tx.listen(0));
  ASSERT_TRUE(tx.listen(0));   // re-listen closes the old fd and makes a new one

  const int flags = ::fcntl(tx.listen_fd(), F_GETFD);
  ASSERT_NE(flags, -1);
  EXPECT_TRUE(flags & FD_CLOEXEC);
}
