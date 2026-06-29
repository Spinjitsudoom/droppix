#pragma once
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace droppix {

// Captures raw PCM (s16le/48000/stereo) from the droppix-audio sink's monitor
// via pw-record (run in the user session) and hands chunks to the stream loop.
// Best-effort: if capture cannot start, drain() simply yields nothing.
class AudioStreamer {
 public:
  ~AudioStreamer() { stop(); }

  // Spawn pw-record under `user_prefix` on droppix-audio.monitor and begin
  // reading. Returns false if the process could not be started.
  bool start(const std::string& user_prefix);

  // Begin reading PCM from an already-open fd (takes ownership of the fd).
  void read_from_fd(int fd);

  // Append all queued PCM chunks to `out`, clear the queue. Returns true if any
  // were added. Non-blocking; safe to call from the stream loop.
  bool drain(std::vector<std::vector<unsigned char>>& out);

  void stop();

 private:
  void reader_loop();

  int fd_ = -1;
  FILE* proc_ = nullptr;             // set when started via popen()
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::mutex mu_;
  std::vector<std::vector<unsigned char>> queue_;   // guarded by mu_
  std::mutex stop_mu_;               // guards stop() for idempotent/concurrent calls
};

}  // namespace droppix
