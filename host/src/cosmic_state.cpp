#include "cosmic_state.h"
#include "desktop_backend.h"   // edid_serial_matches, safe_output_name
#include <cmath>
#include <cstdio>
#include <sstream>
#include <utility>

namespace droppix {

namespace {
std::string quoted(const std::string& line, size_t from = 0) {
  const size_t a = line.find('"', from);
  if (a == std::string::npos) return {};
  const size_t b = line.find('"', a + 1);
  return b == std::string::npos ? std::string{} : line.substr(a + 1, b - a - 1);
}
std::string trim(const std::string& s) {
  const size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  return s.substr(a, s.find_last_not_of(" \t\r\n") + 1 - a);
}
}  // namespace

bool parse_cosmic_outputs(const std::string& kdl, std::vector<CosmicOutput>& out) {
  std::vector<CosmicOutput> outs;
  struct Extra { int mw = 0, mh = 0; double scale = 1; bool rotated = false; };
  std::vector<Extra> extra;
  std::istringstream in(kdl);
  std::string raw;
  int id = 0;
  while (std::getline(in, raw)) {
    const std::string line = trim(raw);
    if (line.rfind("output ", 0) == 0) {
      CosmicOutput o;
      o.info.name = quoted(line);
      o.info.id = ++id;
      o.info.enabled = line.find("enabled=#true") != std::string::npos;
      outs.push_back(o);
      extra.emplace_back();
      continue;
    }
    if (outs.empty()) continue;
    CosmicOutput& o = outs.back();
    Extra& e = extra.back();
    int a = 0, b = 0, c = 0;
    double s = 0;
    if (std::sscanf(line.c_str(), "position %d %d", &a, &b) == 2) { o.info.geom.x = a; o.info.geom.y = b; }
    else if (std::sscanf(line.c_str(), "scale %lf", &s) == 1 && s > 0) e.scale = s;
    else if (line.rfind("transform ", 0) == 0) {
      const std::string t = quoted(line);
      e.rotated = t.find("90") != std::string::npos || t.find("270") != std::string::npos;
    }
    else if (line == "xwayland_primary #true") o.info.primary = true;
    else if (line.rfind("serial_number ", 0) == 0) o.serial = quoted(line);
    else if (line.rfind("mirroring ", 0) == 0) o.mirroring = quoted(line);
    else if (std::sscanf(line.c_str(), "mode %d %d %d", &a, &b, &c) == 3 &&
             line.find("current=#true") != std::string::npos) { e.mw = a; e.mh = b; }
  }
  if (outs.empty()) return false;
  for (size_t i = 0; i < outs.size(); ++i) {
    int w = static_cast<int>(std::lround(extra[i].mw / extra[i].scale));
    int h = static_cast<int>(std::lround(extra[i].mh / extra[i].scale));
    if (extra[i].rotated) std::swap(w, h);
    outs[i].info.geom.w = w;
    outs[i].info.geom.h = h;
  }
  // A mirroring output has no mode of its own in the list; it shows its source's area.
  for (auto& o : outs) {
    if (o.mirroring.empty()) continue;
    for (const auto& src : outs)
      if (src.info.name == o.mirroring && src.mirroring.empty()) { o.info.geom = src.info.geom; break; }
  }
  out = std::move(outs);
  return true;
}

std::string cosmic_find_by_serial(const std::vector<CosmicOutput>& outs, uint32_t session_serial) {
  for (const auto& o : outs)
    if (edid_serial_matches(o.serial, session_serial)) return o.info.name;
  return {};
}

std::string cosmic_touch_target(const std::vector<CosmicOutput>& outs, const std::string& name) {
  for (const auto& o : outs)
    if (o.info.name == name) return o.mirroring.empty() ? name : o.mirroring;
  return name;
}

std::string cosmic_input_devices(const std::string& existing, const std::string& device,
                                 const std::string& output) {
  if (!safe_output_name(device) || !safe_output_name(output)) return {};
  std::vector<std::pair<std::string, std::string>> kept;   // raw key, raw value
  const std::string& t = existing;
  size_t i = t.find_first_not_of(" \t\r\n");
  if (i != std::string::npos) {
    if (t[i] != '{') return {};
    ++i;
    bool closed = false;
    while (i < t.size()) {
      while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\r' || t[i] == '\n' || t[i] == ',')) ++i;
      if (i >= t.size()) break;
      if (t[i] == '}') { closed = true; ++i; break; }
      if (t[i] != '"') return {};
      const size_t ks = i++;
      while (i < t.size() && t[i] != '"') { if (t[i] == '\\') ++i; ++i; }
      if (i >= t.size()) return {};
      const std::string key = t.substr(ks, ++i - ks);
      while (i < t.size() && t[i] != ':') {
        if (t[i] != ' ' && t[i] != '\t' && t[i] != '\n' && t[i] != '\r') return {};
        ++i;
      }
      if (i >= t.size()) return {};
      const size_t vs = ++i;
      int depth = 0;
      bool in_str = false;
      for (; i < t.size(); ++i) {
        const char c = t[i];
        if (in_str) { if (c == '\\') ++i; else if (c == '"') in_str = false; continue; }
        if (c == '"') in_str = true;
        else if (c == '(' || c == '[' || c == '{') ++depth;
        else if (c == ')' || c == ']' || c == '}') { if (depth == 0) break; --depth; }
        else if (c == ',' && depth == 0) break;
      }
      if (i >= t.size() || depth != 0 || in_str) return {};
      if (key != "\"" + device + "\"") kept.emplace_back(key, trim(t.substr(vs, i - vs)));
    }
    if (!closed) return {};
  }
  std::string r = "{\n";
  for (const auto& kv : kept) r += "    " + kv.first + ": " + kv.second + ",\n";
  r += "    \"" + device + "\": (state: Enabled, map_to_output: Some(\"" + output + "\")),\n";
  return r + "}\n";
}

}  // namespace droppix
