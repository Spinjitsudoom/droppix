#include "monitor_geometry.h"
#include <sstream>
#include <cstdio>
#include <algorithm>

namespace droppix {

std::vector<OutputInfo> parse_kscreen_outputs(const std::string& text) {
  std::vector<OutputInfo> outs;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    auto pos = line.find("Output:");
    if (pos != std::string::npos) {
      // "Output: <num> <name> ..."
      std::istringstream ls(line.substr(pos + 7));
      int num; std::string name;
      ls >> num >> name;
      OutputInfo o; o.name = name; outs.push_back(o);
      continue;
    }
    if (outs.empty()) continue;
    if (line.find("enabled") != std::string::npos &&
        line.find("disabled") == std::string::npos) {
      outs.back().enabled = true;
    }
    auto gp = line.find("Geometry:");
    if (gp != std::string::npos) {
      int x, y, w, h;
      // "Geometry: X,Y WxH"
      if (std::sscanf(line.c_str() + gp, "Geometry: %d,%d %dx%d", &x, &y, &w, &h) == 4) {
        outs.back().geom = Rect{x, y, w, h};
      }
    }
  }
  return outs;
}

Rect desktop_bounds(const std::vector<OutputInfo>& outs) {
  int right = 0, bottom = 0;
  for (const auto& o : outs) {
    if (!o.enabled) continue;
    right = std::max(right, o.geom.x + o.geom.w);
    bottom = std::max(bottom, o.geom.y + o.geom.h);
  }
  return Rect{0, 0, right, bottom};
}

bool select_droppix(const std::vector<OutputInfo>& outs, int mode_w, int mode_h, Rect& out) {
  const OutputInfo* sized = nullptr;
  const OutputInfo* preferred = nullptr;
  for (const auto& o : outs) {
    if (!o.enabled) continue;
    if (o.geom.w == mode_w && o.geom.h == mode_h) {
      if (!sized) sized = &o;
      if (o.name.find("evdi") != std::string::npos ||
          o.name.find("Unknown") != std::string::npos ||
          o.name.find("droppix") != std::string::npos) {
        preferred = &o; break;
      }
    }
  }
  const OutputInfo* pick = preferred ? preferred : sized;
  if (!pick) return false;
  out = pick->geom;
  return true;
}
}  // namespace droppix
