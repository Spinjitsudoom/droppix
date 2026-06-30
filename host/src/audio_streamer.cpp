#include "audio_streamer.h"
#include <unistd.h>

namespace droppix {

bool AudioStreamer::start(const std::string& user_prefix) {
  if (running_) return false;
  // parec (PulseAudio compat) reliably resolves the sink's ".monitor" device and
  // emits headerless s16le PCM to stdout. NOTE: `pw-record --target=<name>.monitor`
  // silently fails to bind the monitor here (captures digital silence), so use parec.
  const std::string cmd =
      user_prefix +
      "parec --device=droppix-audio.monitor "
      "--format=s16le --rate=48000 --channels=2 2>/dev/null";
  FILE* p = ::popen(cmd.c_str(), "r");  // blocking; done OUTSIDE stop_mu_
  if (!p) return false;
  std::lock_guard<std::mutex> lk(stop_mu_);
  proc_ = p;
  fd_ = ::fileno(proc_);
  running_ = true;
  thread_ = std::thread(&AudioStreamer::reader_loop, this);
  return true;
}

void AudioStreamer::read_from_fd(int fd) {
  if (running_) return;
  std::lock_guard<std::mutex> lk(stop_mu_);
  fd_ = fd;
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
