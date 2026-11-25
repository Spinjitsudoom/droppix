#include "mutter_state.h"
#include "gvariant_text.h"
#include "desktop_backend.h"   // edid_serial_matches
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace droppix {

const MutterMode* MutterMonitor::current_mode() const {
  for (const auto& m : modes) if (m.current) return &m;
  return nullptr;
}

namespace {

const GVal* unwrap(const GVal* v) {
  while (v && v->kind == GVal::Variant && !v->items.empty()) v = &v->items[0];
  return v;
}

bool is(const GVal& v, GVal::Kind k, size_t min_items = 0) {
  return v.kind == k && v.items.size() >= min_items;
}

// (connector, vendor, product, serial)
bool read_spec(const GVal& v, MutterMonitor& m) {
  if (!is(v, GVal::Tuple, 4)) return false;
  for (int i = 0; i < 4; ++i) if (v.items[i].kind != GVal::Str) return false;
  m.connector = v.items[0].str; m.vendor = v.items[1].str;
  m.product = v.items[2].str; m.serial = v.items[3].str;
  return true;
}

std::string fmt_scale(double s) {
  char buf[40];
  std::snprintf(buf, sizeof buf, "%.17g", s);
  std::string r(buf);
  if (r.find_first_of(".e") == std::string::npos) r += ".0";
  return r;
}

const MutterMonitor* monitor(const MutterState& st, const std::string& connector) {
  for (const auto& m : st.monitors) if (m.connector == connector) return &m;
  return nullptr;
}

// "('DP-1', 'mode-id', @a{sv} {})" for a connector at its current mode (or `mode_id`).
std::string monitor_arg(const MutterState& st, const std::string& connector,
                        const std::string& mode_id = {}) {
  const MutterMonitor* m = monitor(st, connector);
  if (!m) return {};
  std::string id = mode_id;
  if (id.empty()) {
    const MutterMode* cur = m->current_mode();
    if (!cur) return {};
    id = cur->id;
  }
  return "('" + connector + "', '" + id + "', @a{sv} {})";
}

std::string logical_arg(const MutterLogical& l, int x, int y, const std::string& monitors) {
  return "(" + std::to_string(x) + ", " + std::to_string(y) + ", " + fmt_scale(l.scale) +
         ", uint32 " + std::to_string(l.transform) + ", " + (l.primary ? "true" : "false") +
         ", [" + monitors + "])";
}

std::string monitors_of(const MutterState& st, const MutterLogical& l) {
  std::string out;
  for (const auto& c : l.connectors) {
    const std::string a = monitor_arg(st, c);
    if (a.empty()) return {};
    out += (out.empty() ? "" : ", ") + a;
  }
  return out;
}

}  // namespace

bool parse_mutter_state(const std::string& text, MutterState& out) {
  GVal root;
  if (!parse_gvariant_text(text, root) || !is(root, GVal::Tuple, 4)) return false;
  const GVal& serial = root.items[0];
  const GVal& mons = root.items[1];
  const GVal& logs = root.items[2];
  if (serial.kind != GVal::Num || mons.kind != GVal::Array || logs.kind != GVal::Array ||
      root.items[3].kind != GVal::Dict)
    return false;

  MutterState st;
  st.serial = static_cast<uint32_t>(serial.num);
  for (const auto& mv : mons.items) {
    // ((connector, vendor, product, serial), modes, props)
    MutterMonitor m;
    if (!is(mv, GVal::Tuple, 3) || !read_spec(mv.items[0], m) || mv.items[1].kind != GVal::Array)
      return false;
    for (const auto& md : mv.items[1].items) {
      // (id, w, h, refresh, preferred_scale, supported_scales, props)
      if (!is(md, GVal::Tuple, 7) || md.items[0].kind != GVal::Str) return false;
      MutterMode mode;
      mode.id = md.items[0].str;
      mode.w = static_cast<int>(md.items[1].num);
      mode.h = static_cast<int>(md.items[2].num);
      const GVal* cur = unwrap(md.items[6].get("is-current"));
      mode.current = cur && cur->kind == GVal::Bool && cur->b;
      m.modes.push_back(std::move(mode));
    }
    st.monitors.push_back(std::move(m));
  }
  for (const auto& lv : logs.items) {
    // (x, y, scale, transform, primary, [monitor specs], props)
    if (!is(lv, GVal::Tuple, 6) || lv.items[5].kind != GVal::Array) return false;
    MutterLogical l;
    l.x = static_cast<int>(lv.items[0].num);
    l.y = static_cast<int>(lv.items[1].num);
    l.scale = lv.items[2].num > 0 ? lv.items[2].num : 1.0;
    l.transform = static_cast<uint32_t>(lv.items[3].num);
    l.primary = lv.items[4].kind == GVal::Bool && lv.items[4].b;
    for (const auto& sv : lv.items[5].items) {
      MutterMonitor spec;
      if (!read_spec(sv, spec)) return false;
      l.connectors.push_back(spec.connector);
    }
    st.logical.push_back(std::move(l));
  }
  if (const GVal* lm = unwrap(root.items[3].get("layout-mode")); lm && lm->kind == GVal::Num)
    st.layout_mode = static_cast<uint32_t>(lm->num);
  out = std::move(st);
  return true;
}

std::vector<OutputInfo> mutter_outputs(const MutterState& st) {
  std::vector<OutputInfo> outs;
  int id = 0;
  for (const auto& m : st.monitors) {
    OutputInfo o;
    o.name = m.connector;
    o.id = ++id;
    for (const auto& l : st.logical) {
      bool here = false;
      for (const auto& c : l.connectors) here = here || c == m.connector;
      const MutterMode* mode = m.current_mode();
      if (!here || !mode) continue;
      int w = mode->w, h = mode->h;
      if (l.transform % 2 == 1) std::swap(w, h);   // 90° / 270° (and their flips)
      if (st.layout_mode != 2) {
        w = static_cast<int>(std::lround(w / l.scale));
        h = static_cast<int>(std::lround(h / l.scale));
      }
      o.geom = Rect{l.x, l.y, w, h};
      o.enabled = true;
      o.primary = l.primary;
      break;
    }
    outs.push_back(std::move(o));
  }
  return outs;
}

std::string mutter_find_by_serial(const MutterState& st, uint32_t session_serial) {
  for (const auto& m : st.monitors)
    if (edid_serial_matches(m.serial, session_serial)) return m.connector;
  return {};
}

std::string mutter_place_config(const MutterState& st, const std::string& connector, int x, int y) {
  std::string out;
  bool found = false;
  for (const auto& l : st.logical) {
    bool mine = false;
    for (const auto& c : l.connectors) mine = mine || c == connector;
    found = found || mine;
    const std::string mons = monitors_of(st, l);
    if (mons.empty()) return {};
    out += (out.empty() ? "" : ", ") + logical_arg(l, mine ? x : l.x, mine ? y : l.y, mons);
  }
  return found ? "[" + out + "]" : std::string{};
}

std::string mutter_mirror_config(const MutterState& st, const std::string& connector) {
  const MutterLogical* prim = nullptr;
  for (const auto& l : st.logical) if (l.primary) { prim = &l; break; }
  const MutterMonitor* mine = monitor(st, connector);
  if (!prim || !mine || prim->connectors.empty()) return {};
  const MutterMonitor* pm = monitor(st, prim->connectors[0]);
  if (!pm || pm->connector == connector) return {};

  // Largest resolution both monitors offer.
  const MutterMode *best_p = nullptr, *best_m = nullptr;
  for (const auto& a : pm->modes)
    for (const auto& b : mine->modes)
      if (a.w == b.w && a.h == b.h &&
          (!best_p || static_cast<long>(a.w) * a.h > static_cast<long>(best_p->w) * best_p->h ||
           (a.w == best_p->w && a.h == best_p->h && a.current))) {
        best_p = &a; best_m = &b;
      }
  if (!best_p) return {};

  std::string out;
  for (const auto& l : st.logical) {
    bool has_mine = false;
    for (const auto& c : l.connectors) has_mine = has_mine || c == connector;
    if (&l == prim) {
      const std::string mons = monitor_arg(st, pm->connector, best_p->id) + ", " +
                               monitor_arg(st, connector, best_m->id);
      out += (out.empty() ? "" : ", ") + logical_arg(l, l.x, l.y, mons);
    } else if (!has_mine) {
      const std::string mons = monitors_of(st, l);
      if (mons.empty()) return {};
      out += (out.empty() ? "" : ", ") + logical_arg(l, l.x, l.y, mons);
    }
  }
  return "[" + out + "]";
}

std::string mutter_disable_config(const MutterState& st, const std::string& connector) {
  std::string out;
  bool found = false;
  for (const auto& l : st.logical) {
    MutterLogical kept = l;
    kept.connectors.clear();
    for (const auto& c : l.connectors) {
      if (c == connector) found = true;
      else kept.connectors.push_back(c);
    }
    if (kept.connectors.empty()) continue;
    const std::string mons = monitors_of(st, kept);
    if (mons.empty()) return {};
    out += (out.empty() ? "" : ", ") + logical_arg(kept, kept.x, kept.y, mons);
  }
  return found && !out.empty() ? "[" + out + "]" : std::string{};
}

std::string gnome_map_command(bool pen, uint16_t product_id, const MutterMonitor& m) {
  for (const std::string* f : {&m.vendor, &m.product, &m.serial})
    for (char c : *f)
      if (c == '\'' || c == '"' || c == '\\' || c == '$' || c == '`' ||
          static_cast<unsigned char>(c) < 0x20)
        return {};
  char ids[16];
  std::snprintf(ids, sizeof ids, "1209:%04x", product_id);
  const std::string kind = pen ? "tablet" : "touchscreen";
  const std::string dir = pen ? "tablets" : "touchscreens";
  return "gsettings set org.gnome.desktop.peripherals." + kind +
         ":/org/gnome/desktop/peripherals/" + dir + "/" + ids + "/ output \"['" + m.vendor +
         "', '" + m.product + "', '" + m.serial + "']\"";
}

}  // namespace droppix
