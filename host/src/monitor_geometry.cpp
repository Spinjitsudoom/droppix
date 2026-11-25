#include "monitor_geometry.h"
#include <sstream>
#include <cstdio>
#include <algorithm>

namespace droppix {

// kscreen-doctor colorizes its output (ANSI escapes) even through a pipe; strip
// them so sscanf/name parsing sees clean text.
static std::string strip_ansi(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
      i += 2;
      while (i < s.size() && s[i] != 'm') ++i;  // CSI ... 'm'
      if (i < s.size()) ++i;                     // consume the 'm'
    } else {
      out.push_back(s[i++]);
    }
  }
  return out;
}

std::vector<OutputInfo> parse_kscreen_outputs(const std::string& text) {
  std::vector<OutputInfo> outs;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    line = strip_ansi(line);
    auto pos = line.find("Output:");
    if (pos != std::string::npos) {
      // "Output: <num> <name> ..."
      std::istringstream ls(line.substr(pos + 7));
      int num; std::string name, state;
      ls >> num >> name >> state;
      OutputInfo o; o.name = name; o.id = num;
      // Plasma 5 prints the whole output on this one line ("... DVI-I-1 enabled connected
      // priority 2 ... Geometry: 1280,0 1280x800 ..."): the word after the name is the state,
      // and geometry/priority below are read from this same line. Plasma 6 puts each on its
      // own line, so nothing more matches here.
      o.enabled = state == "enabled";
      outs.push_back(o);
    }
    if (outs.empty()) continue;
    // The state is its own bare line. Other lines also say "enabled" ("HDR: enabled",
    // "Wide Color Gamut: enabled") even for a DISABLED output, so substring matching
    // turned disabled monitors back on.
    const auto first = line.find_first_not_of(" \t");
    const std::string bare = first == std::string::npos
        ? std::string() : line.substr(first, line.find_last_not_of(" \t\r") + 1 - first);
    if (bare == "enabled") outs.back().enabled = true;
    else if (bare == "disabled") outs.back().enabled = false;
    auto gp = line.find("Geometry:");
    if (gp != std::string::npos) {
      int x, y, w, h;
      // "Geometry: X,Y WxH"
      if (std::sscanf(line.c_str() + gp, "Geometry: %d,%d %dx%d", &x, &y, &w, &h) == 4) {
        outs.back().geom = Rect{x, y, w, h};
      }
    }
    auto pp = line.find("priority");
    if (pp != std::string::npos) {
      int pr;
      if (std::sscanf(line.c_str() + pp, "priority %d", &pr) == 1) {
        outs.back().primary = (pr == 1);
      }
    }
  }
  return outs;
}

std::vector<OutputInfo> parse_xrandr_outputs(const std::string& text) {
  std::vector<OutputInfo> outs;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    // Output headers start at column 0; mode lines are indented, "Screen N:" is global.
    if (line.empty() || line[0] == ' ' || line[0] == '\t') continue;
    std::istringstream ls(line);
    std::string name, state;
    if (!(ls >> name >> state)) continue;
    if (state != "connected" && state != "disconnected") continue;
    OutputInfo o; o.name = name;
    // Scan the remaining tokens (skipping e.g. "primary") for the active-mode geometry
    // "WxH+X+Y"; a connected-but-inactive output has none before the "(...)" flags.
    std::string tok;
    while (ls >> tok) {
      if (tok == "primary") { o.primary = true; continue; }
      int w, h, x, y;
      if (std::sscanf(tok.c_str(), "%dx%d+%d+%d", &w, &h, &x, &y) == 4) {
        o.geom = Rect{x, y, w, h};
        o.enabled = true;
        break;
      }
      if (!tok.empty() && tok[0] == '(') break;
    }
    outs.push_back(std::move(o));
  }
  return outs;
}

Rect desktop_bounds(const std::vector<OutputInfo>& outs) {
  bool any = false;
  int left = 0, top = 0, right = 0, bottom = 0;
  for (const auto& o : outs) {
    if (!o.enabled) continue;
    if (!any) {
      left = o.geom.x; top = o.geom.y;
      right = o.geom.x + o.geom.w; bottom = o.geom.y + o.geom.h;
      any = true;
      continue;
    }
    left = std::min(left, o.geom.x);
    top = std::min(top, o.geom.y);
    right = std::max(right, o.geom.x + o.geom.w);
    bottom = std::max(bottom, o.geom.y + o.geom.h);
  }
  return Rect{left, top, right - left, bottom - top};
}

namespace {
bool preferred_droppix_name(const std::string& n) {
  // KWin names evdi outputs with evdi/Unknown; X11 exposes the evdi card's connector
  // as a secondary-GPU output "DVI-I-1-<n>" (a real DVI port lacks the provider suffix).
  return n.find("evdi") != std::string::npos ||
         n.find("Unknown") != std::string::npos ||
         n.find("droppix") != std::string::npos ||
         n.rfind("DVI-I-1-", 0) == 0;
}
bool enabled_in(const std::vector<OutputInfo>& outs, const std::string& name) {
  for (const auto& o : outs) if (o.enabled && o.name == name) return true;
  return false;
}
}  // namespace

bool select_droppix(const std::vector<OutputInfo>& outs, const std::vector<OutputInfo>& before,
                    int mode_w, int mode_h, OutputInfo& out) {
  const OutputInfo* sized = nullptr;
  const OutputInfo* preferred = nullptr;
  for (const auto& o : outs) {
    if (!o.enabled) continue;
    if (o.geom.w == mode_w && o.geom.h == mode_h) {
      // The plain size match must never pick an output that was already enabled before
      // the source created its monitor — that is a pre-existing physical screen (e.g. a
      // laptop panel with the same resolution as the tablet). A preferred NAME match is
      // trusted even if pre-existing (session restarts leave the droppix output up).
      if (!sized && !enabled_in(before, o.name)) sized = &o;
      if (preferred_droppix_name(o.name)) { preferred = &o; break; }
    }
  }
  const OutputInfo* pick = preferred ? preferred : sized;
  if (!pick) return false;
  out = *pick;
  return true;
}
bool select_droppix(const std::vector<OutputInfo>& outs, int mode_w, int mode_h, OutputInfo& out) {
  return select_droppix(outs, {}, mode_w, mode_h, out);
}
bool select_droppix(const std::vector<OutputInfo>& outs, int mode_w, int mode_h, Rect& out) {
  OutputInfo o;
  if (!select_droppix(outs, {}, mode_w, mode_h, o)) return false;
  out = o.geom;
  return true;
}

bool select_new_output(const std::vector<OutputInfo>& before,
                       const std::vector<OutputInfo>& after, OutputInfo& out) {
  for (const auto& a : after) {
    if (!a.enabled || a.geom.w <= 0 || a.geom.h <= 0) continue;
    bool existed = false;
    for (const auto& b : before) if (b.name == a.name) { existed = true; break; }
    if (!existed) { out = a; return true; }
  }
  return false;
}
bool select_new_output(const std::vector<OutputInfo>& before,
                       const std::vector<OutputInfo>& after, Rect& out) {
  OutputInfo o;
  if (!select_new_output(before, after, o)) return false;
  out = o.geom;
  return true;
}
}  // namespace droppix
