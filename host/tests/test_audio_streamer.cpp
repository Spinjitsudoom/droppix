#include <gtest/gtest.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#include "audio_streamer.h"
using namespace droppix;

TEST(AudioStreamer, ReadsChunksFromFdAndDrains) {
  int fds[2];
  ASSERT_EQ(pipe(fds), 0);
  AudioStreamer a;
  a.read_from_fd(fds[0]);            // read end; AudioStreamer owns it
  const unsigned char data[] = {1,2,3,4,5,6,7,8};
  ASSERT_EQ(write(fds[1], data, sizeof(data)), (ssize_t)sizeof(data));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::vector<std::vector<unsigned char>> out;
  EXPECT_TRUE(a.drain(out));
  size_t total = 0; for (auto& c : out) total += c.size();
  EXPECT_EQ(total, sizeof(data));

  std::vector<std::vector<unsigned char>> empty;
  EXPECT_FALSE(a.drain(empty));      // queue cleared by the first drain
  close(fds[1]);
  a.stop();
}
