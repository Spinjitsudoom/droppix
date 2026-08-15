#include <gtest/gtest.h>

#include <csignal>

#include "signal_setup.h"

// Regression guard for a confirmed silent-death bug: a web client disconnecting mid-frame
// raised SIGPIPE on the next SSL_write and killed the streamer outright (exit 141), taking
// the listener with it. If this disposition is ever lost, that returns.
TEST(SignalSetup, IgnoreSigpipeSetsIgnoreDisposition) {
  struct sigaction before {};
  ASSERT_EQ(sigaction(SIGPIPE, nullptr, &before), 0);

  droppix::ignore_sigpipe();

  struct sigaction after {};
  ASSERT_EQ(sigaction(SIGPIPE, nullptr, &after), 0);
  EXPECT_EQ(after.sa_handler, SIG_IGN);

  sigaction(SIGPIPE, &before, nullptr);  // restore for sibling tests
}

// A write to a closed pipe must return EPIPE instead of terminating the process — the
// exact behaviour the streamer's send paths rely on to end a session gracefully.
TEST(SignalSetup, WriteToClosedPipeReturnsErrorInsteadOfKilling) {
  struct sigaction before {};
  ASSERT_EQ(sigaction(SIGPIPE, nullptr, &before), 0);
  droppix::ignore_sigpipe();

  int fds[2];
  ASSERT_EQ(pipe(fds), 0);
  ASSERT_EQ(close(fds[0]), 0);  // reader gone

  const char buf[] = "x";
  ssize_t w = write(fds[1], buf, sizeof(buf));  // would SIGPIPE-kill without the guard
  EXPECT_EQ(w, -1);
  EXPECT_EQ(errno, EPIPE);

  close(fds[1]);
  sigaction(SIGPIPE, &before, nullptr);
}
