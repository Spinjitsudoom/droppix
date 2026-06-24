#pragma once
namespace droppix {
struct Settings {
  enum class Source { TestPattern, Evdi };
  Source source = Source::TestPattern;
  int width = 1280, height = 720;
  int fps = 30, bitrate_kbps = 8000, port = 27000;
  bool auto_adb_reverse = true;
};
}  // namespace droppix
