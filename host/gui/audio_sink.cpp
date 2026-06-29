#include "audio_sink.h"
#include <array>
#include <cstdio>
#include <memory>

namespace droppix {
namespace {
std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  size_t b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
}  // namespace

DroppixAudioSink::Runner DroppixAudioSink::default_runner() {
  return [](const std::string& cmd) -> std::pair<bool, std::string> {
    std::unique_ptr<FILE, int (*)(FILE*)> p(::popen(cmd.c_str(), "r"), ::pclose);
    if (!p) return {false, std::string()};
    std::string out; std::array<char, 256> buf;
    while (std::fgets(buf.data(), (int)buf.size(), p.get())) out += buf.data();
    return {true, out};
  };
}

DroppixAudioSink::DroppixAudioSink(Runner runner) : runner_(std::move(runner)) {}

void DroppixAudioSink::ensure() {
  if (created_) return;
  auto [ok, sinks] = runner_("pactl list short sinks 2>/dev/null");
  if (ok && sinks.find("droppix-audio") != std::string::npos) {
    created_ = false;            // adopt an existing sink; don't own it
    return;
  }
  auto [lok, idx] = runner_(
      "pactl load-module module-null-sink sink_name=droppix-audio "
      "sink_properties=device.description=droppix-audio 2>/dev/null");
  std::string id = trim(idx);
  if (lok && !id.empty()) { module_index_ = id; created_ = true; }
}

void DroppixAudioSink::release() {
  if (!created_ || module_index_.empty()) return;
  runner_("pactl unload-module " + module_index_ + " 2>/dev/null");
  created_ = false;
  module_index_.clear();
}

}  // namespace droppix
