#include "audio_streamer.h"
#include <unistd.h>

#include "audio_latency.h"

namespace droppix {

bool AudioStreamer::start(const std::string& user_prefix) {
  if (running_) return false;
  // parec (PulseAudio compat) reliably resolves the sink's ".monitor" device and
  // emits headerless s16le PCM to stdout. NOTE: `pw-record --target=<name>.monitor`
  // silently fails to bind the monitor here (captures digital silence), so use parec.
  const std::string cmd =
      user_prefix +
      "parec --device=droppix-audio.monitor "
      "--format=s16le --rate=48000 --channels=2 "
      // Without this PulseAudio picks a server-side buffer sized for reliability, which can
      // hold hundreds of milliseconds before the first byte ever reaches this pipe — latency
      // no amount of downstream work can recover.
      "--latency-msec=" + std::to_string(kAudioCaptureLatencyMs) + " 2>/dev/null";
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
    queued_bytes_ += static_cast<size_t>(n);
    // Anything that stalls the stream loop (slow link, static screen, long encode) lets PCM
    // pile up here at 192 kB/s. Playback only advances in real time, so a backlog is never
    // caught up — it is permanent lag. Drop the OLDEST audio, which is the stale audio.
    if (size_t drop = audio_overflow_drop_bytes(queued_bytes_)) {
      size_t dropped = 0;
      size_t i = 0;
      while (i < queue_.size() && dropped < drop) {
        dropped += queue_[i].size();
        ++i;
      }
      queue_.erase(queue_.begin(), queue_.begin() + static_cast<long>(i));
      queued_bytes_ -= dropped;
      dropped_bytes_ += dropped;
    }
  }
}

bool AudioStreamer::drain(std::vector<std::vector<unsigned char>>& out) {
  std::lock_guard<std::mutex> lk(mu_);
  if (queue_.empty()) return false;
  for (auto& c : queue_) out.push_back(std::move(c));
  queue_.clear();
  queued_bytes_ = 0;
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
