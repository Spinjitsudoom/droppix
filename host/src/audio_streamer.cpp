#include "audio_streamer.h"
#include <unistd.h>

namespace droppix {

bool AudioStreamer::start(const std::string& user_prefix) {
  const std::string cmd =
      user_prefix +
      "pw-record --raw --target=droppix-audio.monitor "
      "--format=s16 --rate=48000 --channels=2 --latency=20ms - 2>/dev/null";
  proc_ = ::popen(cmd.c_str(), "r");
  if (!proc_) return false;
  fd_ = ::fileno(proc_);
  begin_reading();
  return true;
}

void AudioStreamer::read_from_fd(int fd) {
  fd_ = fd;
  begin_reading();
}

void AudioStreamer::begin_reading() {
  running_ = true;
  thread_ = std::thread(&AudioStreamer::reader_loop, this);
}

void AudioStreamer::reader_loop() {
  unsigned char buf[4096];
  const int fd = fd_;   // capture once: fd_ may be mutated by stop() on another thread
  while (running_) {
    ssize_t n = ::read(fd, buf, sizeof(buf));
    if (n <= 0) break;                       // EOF / error / pipe closed
    std::lock_guard<std::mutex> lk(mu_);
    queue_.emplace_back(buf, buf + n);
  }
}

bool AudioStreamer::drain(std::vector<std::vector<unsigned char>>& out) {
  std::lock_guard<std::mutex> lk(mu_);
  if (queue_.empty()) return false;
  for (auto& c : queue_) out.push_back(std::move(c));
  queue_.clear();
  return true;
}

void AudioStreamer::stop() {
  std::lock_guard<std::mutex> lk(stop_mu_);
  running_ = false;
  if (proc_) { ::pclose(proc_); proc_ = nullptr; fd_ = -1; }  // closes pipe -> reader read() returns
  else if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
  if (thread_.joinable()) thread_.join();
}

}  // namespace droppix
