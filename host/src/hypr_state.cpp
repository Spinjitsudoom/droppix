#include "hypr_state.h"
#include "desktop_backend.h"   // edid_serial_matches, safe_output_name
#include "json_value.h"
#include <cctype>
#include <cmath>
#include <cstdio>

namespace droppix {

namespace {
std::string num(double v) {
  char b[32];
  std::snprintf(b, sizeof b, "%g", v);
  return b;
}
const HyprMonitor* find(const std::vector<HyprMonitor>& mons, const std::string& name) {
  for (const auto& m : mons) if (m.info.name == name) return &m;
  return nullptr;
}
std::string mode(const HyprMonitor& m) {
  return std::to_string(m.mode_w) + "x" + std::to_string(m.mode_h) + "@" + num(m.refresh);
}
}  // namespace

bool parse_hypr_monitors(const std::string& json, std::vector<HyprMonitor>& out) {
  JVal root;
  if (!parse_json(json, root) || root.kind != JVal::Array) return false;
  std::vector<HyprMonitor> mons;
  for (const auto& o : root.items) {
    if (o.kind != JVal::Object) return false;
    HyprMonitor m;
    m.info.name = o.get_str("name");
    m.info.id = static_cast<int>(o.get_num("id")) + 1;
    m.info.enabled = !o.get_bool("disabled");
    m.serial = o.get_str("serial");
    m.mode_w = static_cast<int>(o.get_num("width"));
    m.mode_h = static_cast<int>(o.get_num("height"));
    m.refresh = o.get_num("refreshRate");
    m.scale = o.get_num("scale", 1) > 0 ? o.get_num("scale", 1) : 1;
    m.transform = static_cast<int>(o.get_num("transform"));
    int w = static_cast<int>(std::lround(m.mode_w / m.scale));
    int h = static_cast<int>(std::lround(m.mode_h / m.scale));
    if (m.transform % 2 == 1) std::swap(w, h);
    m.info.geom = Rect{static_cast<int>(o.get_num("x")), static_cast<int>(o.get_num("y")), w, h};
    mons.push_back(std::move(m));
  }
  // Hyprland has no "primary"; the focused monitor is the closest equivalent.
  for (size_t i = 0; i < root.items.size(); ++i)
    if (root.items[i].get_bool("focused")) mons[i].info.primary = true;
  out = std::move(mons);
  return true;
}

std::string hypr_find_by_serial(const std::vector<HyprMonitor>& mons, uint32_t session_serial) {
  for (const auto& m : mons)
    if (edid_serial_matches(m.serial, session_serial)) return m.info.name;
  return {};
}

std::string hypr_place_rule(const std::vector<HyprMonitor>& mons, const std::string& name, int x, int y) {
  const HyprMonitor* m = find(mons, name);
  if (!m || !safe_output_name(name)) return {};
  std::string r = name + "," + mode(*m) + "," + std::to_string(x) + "x" + std::to_string(y) + "," +
                  num(m->scale);
  if (m->transform) r += ",transform," + std::to_string(m->transform);
  return r;
}

std::string hypr_mirror_rule(const std::vector<HyprMonitor>& mons, const std::string& name,
                             const std::string& primary) {
  const HyprMonitor* m = find(mons, name);
  if (!m || !safe_output_name(name) || !safe_output_name(primary)) return {};
  return name + "," + mode(*m) + ",auto,1,mirror," + primary;
}

std::string pick_hypr_instance(const std::vector<std::string>& signatures,
                               const std::function<long(const std::string&)>& pid_of,
                               const std::function<bool(long)>& alive) {
  for (const auto& s : signatures) {
    if (s.empty() || s.size() > 128) continue;
    bool safe = true;
    for (char c : s) safe = safe && (std::isalnum(static_cast<unsigned char>(c)) || c == '_');
    if (!safe) continue;
    const long pid = pid_of(s);
    if (pid > 0 && alive(pid)) return s;
  }
  return {};
}

}  // namespace droppix
