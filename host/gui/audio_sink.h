#pragma once
#include <functional>
#include <string>
#include <utility>

namespace droppix {

// Owns the lifecycle of the 'droppix-audio' PipeWire null-sink in the user
// session. ensure() adopts an existing sink or creates one; release() unloads
// only a sink this instance created. pactl is invoked via an injectable runner.
class DroppixAudioSink {
 public:
  // Runs a shell command; returns {exit_ok, captured stdout}.
  using Runner = std::function<std::pair<bool, std::string>(const std::string&)>;

  explicit DroppixAudioSink(Runner runner = default_runner());
  ~DroppixAudioSink() { release(); }

  void ensure();                 // idempotent
  void release();                // unload only if created_here()
  bool created_here() const { return created_; }

  static Runner default_runner();   // popen-based, captures stdout

 private:
  Runner runner_;
  bool created_ = false;
  std::string module_index_;     // index from load-module, used by unload-module
};

}  // namespace droppix
