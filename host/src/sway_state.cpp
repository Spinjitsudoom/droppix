#include "sway_state.h"
#include "desktop_backend.h"   // edid_serial_matches
#include "json_value.h"
#include <cctype>

namespace droppix {

bool parse_sway_outputs(const std::string& json, std::vector<SwayOutput>& out) {
  JVal root;
  if (!parse_json(json, root) || root.kind != JVal::Array) return false;
  std::vector<SwayOutput> outs;
  int id = 0;
  for (const auto& o : root.items) {
    if (o.kind != JVal::Object) return false;
    SwayOutput s;
    s.info.name = o.get_str("name");
    s.info.id = ++id;
    s.info.enabled = o.get_bool("active");
    s.info.primary = o.get_bool("primary");
    if (const JVal* r = o.get("rect"))
      s.info.geom = Rect{static_cast<int>(r->get_num("x")), static_cast<int>(r->get_num("y")),
                         static_cast<int>(r->get_num("width")), static_cast<int>(r->get_num("height"))};
    s.serial = o.get_str("serial");
    outs.push_back(std::move(s));
  }
  out = std::move(outs);
  return true;
}

std::string sway_find_by_serial(const std::vector<SwayOutput>& outs, uint32_t session_serial) {
  for (const auto& o : outs)
    if (edid_serial_matches(o.serial, session_serial)) return o.info.name;
  return {};
}

std::string sway_place_command(const std::vector<SwayOutput>& outs, const std::string& name,
                               int x, int y) {
  if (!safe_output_name(name)) return {};
  bool found = false;
  std::string cmd;
  for (const auto& o : outs) {
    if (o.info.name == name) { found = true; continue; }
    if (!o.info.enabled || !safe_output_name(o.info.name)) continue;
    cmd += "output " + o.info.name + " pos " + std::to_string(o.info.geom.x) + " " +
           std::to_string(o.info.geom.y) + "; ";
  }
  if (!found) return {};
  return cmd + "output " + name + " pos " + std::to_string(x) + " " + std::to_string(y);
}

std::string sway_input_identifier(const std::string& json, const std::string& name) {
  JVal root;
  if (!parse_json(json, root) || root.kind != JVal::Array) return {};
  for (const auto& d : root.items) {
    if (d.get_str("name") != name) continue;
    const std::string id = d.get_str("identifier");
    for (char c : id)
      if (!std::isalnum(static_cast<unsigned char>(c)) && c != ':' && c != '-' && c != '_') return {};
    return id;
  }
  return {};
}

std::string pick_sway_socket(const std::vector<std::string>& entries, long uid,
                             const std::function<bool(long pid)>& alive) {
  const std::string prefix = "sway-ipc." + std::to_string(uid) + ".";
  for (const auto& e : entries) {
    if (e.rfind(prefix, 0) != 0 || e.size() <= prefix.size() + 5) continue;
    if (e.compare(e.size() - 5, 5, ".sock") != 0) continue;
    const std::string pid = e.substr(prefix.size(), e.size() - prefix.size() - 5);
    if (pid.empty() || pid.size() > 9) continue;
    bool digits = true;
    for (char c : pid) digits = digits && std::isdigit(static_cast<unsigned char>(c));
    if (digits && alive(std::stol(pid))) return e;
  }
  return {};
}

}  // namespace droppix
