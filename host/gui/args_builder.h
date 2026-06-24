#pragma once
#include <string>
#include <vector>
#include "settings.h"
namespace droppix {
struct Command {
  std::string program;             // droppix_stream path, or "pkexec"
  std::vector<std::string> args;   // arguments (incl. the binary path if pkexec)
  bool needs_adb_reverse = false;
};
Command build_command(const Settings& s, const std::string& stream_bin);
}  // namespace droppix
