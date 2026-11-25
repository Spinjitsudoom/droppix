#include "desktop_backend.h"
#include "mutter_state.h"
#include "sway_state.h"
#include "hypr_state.h"
#include <fstream>
#include <csignal>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <dirent.h>
#include <pwd.h>
#include <unistd.h>

namespace droppix {

std::string pick_wayland_display(const std::vector<std::string>& entries) {
  std::set<std::string> names(entries.begin(), entries.end());
  long best = -1;
  for (const std::string& e : names) {
    if (e.rfind("wayland-", 0) != 0) continue;
    const std::string num = e.substr(8);
    if (num.empty() || num.size() > 6 ||
        !std::all_of(num.begin(), num.end(), [](unsigned char c) { return std::isdigit(c); }))
      continue;
    if (!names.count(e + ".lock")) continue;   // stale socket from a dead compositor
    const long n = std::stol(num);
    if (best < 0 || n < best) best = n;
  }
  return best < 0 ? "wayland-0" : "wayland-" + std::to_string(best);
}

bool has_live_wayland_socket(const std::vector<std::string>& entries) {
  std::set<std::string> names(entries.begin(), entries.end());
  for (const std::string& e : names) {
    if (e.rfind("wayland-", 0) != 0 || e.size() <= 8) continue;
    const std::string num = e.substr(8);
    if (num.size() > 6 || !std::all_of(num.begin(), num.end(),
                                       [](unsigned char c) { return std::isdigit(c); }))
      continue;
    if (names.count(e + ".lock")) return true;
  }
  return false;
}

std::string wayland_session_env(const std::vector<std::string>& entries) {
  return has_live_wayland_socket(entries) ? "XDG_SESSION_TYPE=wayland QT_QPA_PLATFORM='wayland;xcb' "
                                          : std::string{};
}

bool edid_serial_matches(const std::string& s, uint32_t serial) {
  if (s.empty()) return false;
  const bool hex = s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
  const size_t start = hex ? 2 : 0;
  if (start >= s.size()) return false;
  for (size_t i = start; i < s.size(); ++i)
    if (!(hex ? std::isxdigit(static_cast<unsigned char>(s[i]))
              : std::isdigit(static_cast<unsigned char>(s[i]))))
      return false;
  if (s.size() - start > 10) return false;
  return std::strtoul(s.c_str() + start, nullptr, hex ? 16 : 10) == serial;
}

namespace {
// Plasma does not always run on wayland-0 (wayland-1 is common), and a wrong name made
// kscreen-doctor return nothing: the droppix output was never identified and touch was
// disabled.
std::vector<std::string> list_dir(const std::string& dir) {
  std::vector<std::string> entries;
  if (DIR* d = ::opendir(dir.c_str())) {
    while (dirent* e = ::readdir(d)) entries.emplace_back(e->d_name);
    ::closedir(d);
  }
  return entries;
}
std::string discover_wayland_display(const std::string& runtime_dir) {
  return pick_wayland_display(list_dir(runtime_dir));
}
// The live Sway IPC socket in the user's runtime dir ("" when Sway isn't running).
// The live Hyprland instance signature under $XDG_RUNTIME_DIR/hypr ("" if none).
std::string discover_hypr_instance(const std::string& runtime_dir) {
  std::vector<std::string> sigs;
  for (const auto& e : list_dir(runtime_dir + "/hypr")) if (e != "." && e != "..") sigs.push_back(e);
  return pick_hypr_instance(sigs,
      [&](const std::string& sig) -> long {
        std::ifstream f(runtime_dir + "/hypr/" + sig + "/hyprland.lock");
        long pid = 0;
        return (f >> pid) ? pid : 0;
      },
      [](long pid) { return ::kill(static_cast<pid_t>(pid), 0) == 0; });
}
std::string discover_sway_socket(const std::string& runtime_dir, long uid) {
  const std::string s = pick_sway_socket(list_dir(runtime_dir), uid,
                                         [](long pid) { return ::kill(static_cast<pid_t>(pid), 0) == 0; });
  return s.empty() ? s : runtime_dir + "/" + s;
}
}  // namespace

std::string user_session_prefix() {
  const char* uid = std::getenv("PKEXEC_UID");
  if (!uid || !*uid) uid = std::getenv("SUDO_UID");
  if (!uid || !*uid) return "env ";  // already in a user session
  const std::string u(uid);
  const std::string runtime = "/run/user/" + u;
  const std::string env =
      "env XDG_RUNTIME_DIR=" + runtime + " "
      "DBUS_SESSION_BUS_ADDRESS=unix:path=" + runtime + "/bus "
      "WAYLAND_DISPLAY=" + discover_wayland_display(runtime) + " " +
      wayland_session_env(list_dir(runtime)) +
      "DISPLAY=:0 " +   // X11 tools (xrandr/xinput); same "common default" shortcut as wayland-0
      [&]{ const std::string s = discover_sway_socket(runtime, std::atol(u.c_str()));
           return s.empty() ? std::string{} : "SWAYSOCK=" + s + " "; }() +
      [&]{ const std::string h = discover_hypr_instance(runtime);
           return h.empty() ? std::string{} : "HYPRLAND_INSTANCE_SIGNATURE=" + h + " "; }();
  struct passwd* pw = getpwuid(static_cast<uid_t>(std::atoi(u.c_str())));
  if (pw && pw->pw_name) return std::string("runuser -u ") + pw->pw_name + " -- " + env;
  return "sudo -u '#" + u + "' " + env;
}

// Output names are short connector ids (DP-3, HDMI-A-3, ...); reject anything else
// so the name can be safely interpolated into the bind shell command.
bool safe_output_name(const std::string& s) {
  if (s.empty() || s.size() > 64) return false;
  for (char c : s) if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') return false;
  return true;
}

std::vector<OutputInfo> KWinBackend::outputs() {
  std::string out;
  std::string cmd = "timeout 3 " + user_session_prefix() + "kscreen-doctor -o 2>/dev/null";
  FILE* p = popen(cmd.c_str(), "r");
  if (p) {
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    pclose(p);
  }
  std::fprintf(stderr, "input: kscreen query returned %zu bytes\n", out.size());
  return parse_kscreen_outputs(out);
}

// Map the droppix-touch device onto the droppix output via KWin's per-device DBus
// properties (mapToWorkspace=false + outputName=<droppix>). Reads/writes via
// org.freedesktop.DBus.Properties (the qdbus shorthand silently errors on these
// objects). Retries while KWin registers the new uinput device.
void KWinBackend::map_touch(const std::string& output_name, const std::string& touch_name) {
  if (!safe_output_name(output_name)) return;
  if (!safe_output_name(touch_name)) return;   // shell-injected into the sh -c string below; the allowlist matters
  const std::string inner =
      "QD=; for q in qdbus6 qdbus-qt6 qdbus; do command -v \"$q\" >/dev/null 2>&1 && QD=$q && break; done; "
      "[ -z \"$QD\" ] && { echo \"[touch-bind] no qdbus available\" >&2; exit 0; }; "
      "I=org.kde.KWin.InputDevice; PG=org.freedesktop.DBus.Properties.Get; PS=org.freedesktop.DBus.Properties.Set; "
      "for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do "
      // devicesSysNames, not ListTouch: Plasma 5.27 has no ListTouch, so touch was never bound
      // there. Both Plasma 5 and 6 have the property; the name check below picks our device.
      "for d in $(\"$QD\" org.kde.KWin /org/kde/KWin/InputDevice $PG "
      "org.kde.KWin.InputDeviceManager devicesSysNames 2>/dev/null); do "
      "P=/org/kde/KWin/InputDevice/$d; "
      "n=$(\"$QD\" org.kde.KWin \"$P\" $PG $I name 2>/dev/null); "
      "if [ \"$n\" = " + touch_name + " ]; then "
      "echo \"[touch-bind] found droppix-touch ($d) before mapToWorkspace=$(\"$QD\" org.kde.KWin \"$P\" $PG $I mapToWorkspace 2>/dev/null) outputName=[$(\"$QD\" org.kde.KWin \"$P\" $PG $I outputName 2>/dev/null)] target=" +
      output_name + "\" >&2; "
      "\"$QD\" org.kde.KWin \"$P\" $PS $I mapToWorkspace false 2>&1 | sed \"s/^/[touch-bind] set mapToWorkspace: /\" >&2; "
      "\"$QD\" org.kde.KWin \"$P\" $PS $I outputName " + output_name +
      " 2>&1 | sed \"s/^/[touch-bind] set outputName: /\" >&2; "
      "echo \"[touch-bind] after mapToWorkspace=$(\"$QD\" org.kde.KWin \"$P\" $PG $I mapToWorkspace 2>/dev/null) outputName=[$(\"$QD\" org.kde.KWin \"$P\" $PG $I outputName 2>/dev/null)]\" >&2; "
      "exit 0; fi; done; sleep 0.2; done; "
      "echo \"[touch-bind] droppix-touch not found via devicesSysNames after retries\" >&2";
  std::string cmd = "timeout 10 " + user_session_prefix() + "sh -c '" + inner + "'";
  std::system(cmd.c_str());
}

// Best-effort pen->output binding on KWin. The pen is a *tablet* device, not a touch
// device, so it never appears in InputDeviceManager.ListTouch (used by map_touch above).
// Instead, enumerate ALL connected input devices via InputDeviceManager's
// `devicesSysNames` property (verified against KWin's libinput Connection DBus adaptor:
// org.kde.KWin.InputDeviceManager, Q_PROPERTY QStringList devicesSysNames, alongside the
// narrower ListPointers/ListTouch/ListKeyboards) -- then bind the match the same way
// map_touch does: org.kde.KWin.InputDevice Properties.Set mapToWorkspace=false +
// outputName=<output>. Whether KWin actually honors outputName for a tablet node (as
// opposed to a touchscreen) is unconfirmed until Task 7's on-device pass; if the device
// can't be found/bound, this logs a warning and returns false -- droppix keeps working,
// the pen may just land on the wrong monitor until that's resolved (fallback idea:
// scale_x/scale_y like the aux pointer).
bool KWinBackend::map_pen(const std::string& output_name, const std::string& pen_name) {
  if (!safe_output_name(output_name)) return false;
  if (!safe_output_name(pen_name)) return false;   // shell-interpolated below; allowlist matters
  const std::string inner =
      "QD=; for q in qdbus6 qdbus-qt6 qdbus; do command -v \"$q\" >/dev/null 2>&1 && QD=$q && break; done; "
      "[ -z \"$QD\" ] && { echo \"[pen-bind] no qdbus available\" >&2; exit 1; }; "
      "I=org.kde.KWin.InputDevice; M=org.kde.KWin.InputDeviceManager; "
      "PG=org.freedesktop.DBus.Properties.Get; PS=org.freedesktop.DBus.Properties.Set; "
      "for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do "
      "for d in $(\"$QD\" org.kde.KWin /org/kde/KWin/InputDevice $PG $M devicesSysNames 2>/dev/null); do "
      "P=/org/kde/KWin/InputDevice/$d; "
      "n=$(\"$QD\" org.kde.KWin \"$P\" $PG $I name 2>/dev/null); "
      "if [ \"$n\" = " + pen_name + " ]; then "
      "echo \"[pen-bind] found droppix-pen ($d) before mapToWorkspace=$(\"$QD\" org.kde.KWin \"$P\" $PG $I mapToWorkspace 2>/dev/null) outputName=[$(\"$QD\" org.kde.KWin \"$P\" $PG $I outputName 2>/dev/null)] target=" +
      output_name + "\" >&2; "
      "\"$QD\" org.kde.KWin \"$P\" $PS $I mapToWorkspace false 2>&1 | sed \"s/^/[pen-bind] set mapToWorkspace: /\" >&2; "
      "\"$QD\" org.kde.KWin \"$P\" $PS $I outputName " + output_name +
      " 2>&1 | sed \"s/^/[pen-bind] set outputName: /\" >&2; "
      "echo \"[pen-bind] after mapToWorkspace=$(\"$QD\" org.kde.KWin \"$P\" $PG $I mapToWorkspace 2>/dev/null) outputName=[$(\"$QD\" org.kde.KWin \"$P\" $PG $I outputName 2>/dev/null)]\" >&2; "
      "exit 0; fi; done; sleep 0.2; done; "
      "echo \"[pen-bind] droppix-pen not found via devicesSysNames after retries\" >&2; exit 1";
  std::string cmd = "timeout 10 " + user_session_prefix() + "sh -c '" + inner + "'";
  if (std::system(cmd.c_str()) == 0) return true;
  std::fprintf(stderr, "[pen-bind] KWin pen output-binding unavailable; pen may land on the wrong monitor\n");
  return false;
}

std::vector<OutputInfo> X11Backend::outputs() {
  std::string out;
  // timeout 8: during a GPU hotplug (the evdi output appearing) X blocks queries while
  // it re-probes outputs; 3s gets killed mid-reconfigure and returns nothing.
  std::string cmd = "timeout 8 " + user_session_prefix() + "xrandr --query 2>/dev/null";
  FILE* p = popen(cmd.c_str(), "r");
  if (p) {
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    pclose(p);
  }
  std::fprintf(stderr, "input: xrandr query returned %zu bytes\n", out.size());
  return parse_xrandr_outputs(out);
}

// Pin an absolute input device (touch or pen) to the droppix output via `xinput
// map-to-output`. Shared by map_touch and map_pen -- touch and pen are both absolute
// devices, so the same binding logic applies verbatim; `tag` only distinguishes the two
// in logs. Retries while X registers the new uinput device (mirrors KWinBackend's wait
// on ListTouch/devicesSysNames). Caller must pass names that pass safe_output_name.
//
// map-to-output bakes the output's CURRENT geometry into the device's transform matrix,
// so binding before adopt_output's placement has settled in X pins the device to the
// output's stale position (wrong screen). Once the device appears, RE-APPLY the mapping
// until the target output's geometry stops changing (idempotent + cheap), so the final
// binding reflects the settled layout.
bool X11Backend::map_device_x11(const std::string& output_name, const std::string& dev_name,
                                const std::string& tag) {
  if (!safe_output_name(output_name)) return false;
  if (!safe_output_name(dev_name)) return false;   // shell-interpolated below; allowlist matters
  const std::string inner =
      "N=" + output_name + "; T=" + dev_name + "; "
      "geom() { xrandr --query 2>/dev/null | awk -v n=\"$N\" \"\\$1==n{"
      "for(i=1;i<=NF;i++) if(\\$i ~ /[0-9]+x[0-9]+\\+[0-9]+\\+[0-9]+/){print \\$i; exit}}\"; }; "
      "found=0; last=; stable=0; "
      "for i in $(seq 1 25); do "
      "if xinput list --name-only 2>/dev/null | grep -Fxq \"$T\"; then found=1; "
      "xinput map-to-output \"$T\" \"$N\" 2>&1 | sed \"s/^/[" + tag + "] map-to-output: /\" >&2; "
      "g=$(geom); "
      "if [ -n \"$g\" ] && [ \"$g\" = \"$last\" ]; then stable=$((stable+1)); "
      "[ \"$stable\" -ge 3 ] && { echo \"[" + tag + "] mapped $T -> $N @ $g\" >&2; exit 0; }; "
      "else stable=0; fi; last=$g; "
      "fi; sleep 0.2; done; "
      "[ \"$found\" = 1 ] && echo \"[" + tag + "] mapped $T -> $N (layout unsettled)\" >&2 || "
      "echo \"[" + tag + "] $T not found via xinput after retries\" >&2";
  std::string cmd = "timeout 10 " + user_session_prefix() + "sh -c '" + inner + "'";
  std::system(cmd.c_str());
  return true;
}

void X11Backend::map_touch(const std::string& output_name, const std::string& touch_name) {
  map_device_x11(output_name, touch_name, "touch-bind");
}

// Bind the pen device to the droppix output. `xinput map-to-output` works for any
// absolute device (not just touch), so this is the identical binding, just a different
// device name/log tag -- see map_device_x11.
bool X11Backend::map_pen(const std::string& output_name, const std::string& pen_name) {
  return map_device_x11(output_name, pen_name, "pen-bind");
}

// Adopt the just-appeared droppix output on X11. Xorg hot-adds the evdi card as a
// second GPU but nothing renders into it until it is linked to the primary GPU as a
// reverse-PRIME sink; and the desktop's auto-reconfigure can scramble the layout
// (black screens). So: link the providers, place the droppix output right of the
// primary, and re-assert the primary — an explicit, sane layout.
// Reverse-PRIME link + bare mode-enable, run BEFORE the droppix output is identifiable
// (no output name needed). Two things a separate-GPU evdi provider needs before X will
// ever assign it a mode — and evdi's wait_for_mode blocks until a mode lands:
//   1) attach every provider beyond 0 as a sink of provider 0, so its outputs appear in
//      `xrandr --query` at all;
//   2) `--auto` any output that is now "connected" but has no mode yet (X links a new
//      provider's output as disabled — it does not auto-enable it the way KWin does).
// Placement (--right-of the primary) is deliberately left to the existing adopt_output,
// called later once the daemon has identified the output by its before/after diff;
// this only needs to produce *a* mode so the evdi mode-wait can return. Idempotent.
// Safe to call while the mode-wait pumps events on another thread — it only runs xrandr
// subprocesses, never touching the evdi handle.
bool X11Backend::link_providers() {
  const std::string inner =
      "np=$(xrandr --listproviders 2>/dev/null | grep -c \"^Provider\"); "
      "i=1; while [ \"$i\" -lt \"${np:-1}\" ]; do "
      "xrandr --setprovideroutputsource \"$i\" 0 2>&1 | sed \"s/^/[provider-link] provider $i: /\" >&2; "
      "i=$((i+1)); done; "
      // $2=="connected" && $3 starts with "(" => connected but no current mode/geometry
      // (an enabled output's 3rd field is its WxH+X+Y, not the "(normal ...)" flags group).
      "for o in $(xrandr --query 2>/dev/null | awk '$2==\"connected\" && $3 ~ /^\\(/{print $1}'); do "
      "xrandr --output \"$o\" --auto 2>&1 | sed \"s/^/[provider-link] auto $o: /\" >&2; "
      "done";
  std::string cmd = "timeout 10 " + user_session_prefix() + "sh -c '" + inner + "'";
  std::system(cmd.c_str());
  return true;
}

bool X11Backend::adopt_output(const std::string& output_name) {
  if (!safe_output_name(output_name)) return false;
  const std::string inner =
      "N=" + output_name + "; "
      // 1) Reverse-PRIME link: any provider beyond 0 sinks from provider 0.
      "np=$(xrandr --listproviders 2>/dev/null | grep -c \"^Provider\"); "
      "i=1; while [ \"$i\" -lt \"${np:-1}\" ]; do "
      "xrandr --setprovideroutputsource \"$i\" 0 2>&1 | sed \"s/^/[output-adopt] provider $i: /\" >&2; "
      "i=$((i+1)); done; "
      // 2) Find the primary (or any other connected) output to anchor the layout.
      "P=$(xrandr --query 2>/dev/null | awk \"/ connected primary/{print \\$1; exit}\"); "
      "[ -z \"$P\" ] || [ \"$P\" = \"$N\" ] && "
      "P=$(xrandr --query 2>/dev/null | awk -v d=\"$N\" \"\\$2==\\\"connected\\\" && \\$1!=d {print \\$1; exit}\"); "
      "[ -z \"$P\" ] && { echo \"[output-adopt] no anchor output found\" >&2; exit 0; }; "
      // 3) Explicit layout: droppix right of the primary, primary stays primary.
      "xrandr --output \"$N\" --auto --right-of \"$P\" 2>&1 | sed \"s/^/[output-adopt] place: /\" >&2; "
      "xrandr --output \"$P\" --auto --primary 2>&1 | sed \"s/^/[output-adopt] primary: /\" >&2; "
      "echo \"[output-adopt] $N placed right-of $P\" >&2";
  std::string cmd = "timeout 10 " + user_session_prefix() + "sh -c '" + inner + "'";
  std::system(cmd.c_str());
  return true;
}

// Shared apply_layout body for KWin/X11: find the primary output (preferring the
// compositor-reported primary, else the first other enabled output), build the
// compositor command via layout_command, and run it the same way adopt_output does.
static bool run_layout(BackendKind kind, const std::string& evdi, LayoutMode mode,
                       const std::vector<OutputInfo>& outs) {
  // find the primary output (skip the evdi output itself)
  const OutputInfo* p = nullptr;
  for (const auto& o : outs) {
    if (o.name == evdi || !o.enabled) continue;
    if (o.primary) { p = &o; break; }
    if (!p) p = &o;                        // fallback: first other enabled output
  }
  if (!p) { std::fprintf(stderr, "[layout] no primary output found\n"); return false; }
  std::string base = layout_command(kind, evdi, p->name, p->id, mode);
  if (base.empty()) {
    std::fprintf(stderr, "[layout] mirror/extend unsupported on this compositor\n");
    return false;
  }
  std::string cmd = "timeout 10 " + user_session_prefix() + "sh -c '" + base + "'";
  std::system(cmd.c_str());
  std::fprintf(stderr, "[layout] applied %s for %s\n",
               mode == LayoutMode::Mirror ? "mirror" : "extend", evdi.c_str());
  return true;
}

bool KWinBackend::apply_layout(const std::string& evdi, LayoutMode mode) {
  if (!safe_output_name(evdi)) return false;
  return run_layout(BackendKind::KWin, evdi, mode, outputs());
}

bool X11Backend::apply_layout(const std::string& evdi, LayoutMode mode) {
  if (!safe_output_name(evdi)) return false;
  return run_layout(BackendKind::X11, evdi, mode, outputs());
}

bool KWinBackend::place_output(const std::string& evdi, int x, int y) {
  if (!safe_output_name(evdi)) return false;
  std::string base = place_command(BackendKind::KWin, evdi, x, y);
  if (base.empty()) return false;
  std::string cmd = "timeout 10 " + user_session_prefix() + "sh -c '" + base + "'";
  std::system(cmd.c_str());
  return true;
}

bool X11Backend::place_output(const std::string& evdi, int x, int y) {
  if (!safe_output_name(evdi)) return false;
  std::string base = place_command(BackendKind::X11, evdi, x, y);
  if (base.empty()) return false;
  std::string cmd = "timeout 10 " + user_session_prefix() + "sh -c '" + base + "'";
  std::system(cmd.c_str());
  return true;
}

// Unsupported compositor: logs a warning and no-ops (display still works via evdi).
// ---- GNOME (Mutter) ----

namespace {
std::string capture(const std::string& cmd) {
  std::string out;
  if (FILE* p = popen(cmd.c_str(), "r")) {
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    pclose(p);
  }
  while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
  return out;
}

std::string shell_quote(const std::string& s) {
  std::string q = "'";
  for (char c : s) { if (c == '\'') q += "'\\''"; else q.push_back(c); }
  return q + "'";
}

const char* kMutterCall =
    "gdbus call --session --dest org.gnome.Mutter.DisplayConfig "
    "--object-path /org/gnome/Mutter/DisplayConfig --method org.gnome.Mutter.DisplayConfig.";

bool read_mutter(MutterState& st) {
  const std::string out = capture("timeout 5 " + user_session_prefix() + kMutterCall +
                                  "GetCurrentState 2>&1");
  if (parse_mutter_state(out, st)) return true;
  std::fprintf(stderr, "[gnome] could not read the display layout: %.200s\n", out.c_str());
  return false;
}

// Temporary (method 1): applies now, never written to the user's monitors.xml.
bool apply_mutter(const MutterState& st, const std::string& config, const char* what) {
  const std::string out = capture("timeout 5 " + user_session_prefix() + kMutterCall +
                                  "ApplyMonitorsConfig " + std::to_string(st.serial) + " 1 " +
                                  shell_quote(config) + " '@a{sv} {}' 2>&1");
  if (out == "()") { std::fprintf(stderr, "[gnome] %s applied\n", what); return true; }
  std::fprintf(stderr, "[gnome] %s rejected: %.300s\n", what, out.c_str());
  return false;
}

bool gnome_map(bool pen, const std::string& output, uint16_t product_id) {
  if (!safe_output_name(output)) return false;
  MutterState st;
  if (!read_mutter(st)) return false;
  for (const auto& m : st.monitors) {
    if (m.connector != output) continue;
    const std::string cmd = gnome_map_command(pen, product_id, m);
    if (cmd.empty()) break;
    const std::string out = capture("timeout 5 " + user_session_prefix() + cmd + " 2>&1");
    std::fprintf(stderr, "[gnome] %s 1209:%04x -> %s (%s %s %s)%s%s\n", pen ? "pen" : "touch",
                 product_id, output.c_str(), m.vendor.c_str(), m.product.c_str(),
                 m.serial.c_str(), out.empty() ? "" : ": ", out.c_str());
    return out.empty();
  }
  std::fprintf(stderr, "[gnome] %s: no usable monitor identity for %s\n", pen ? "pen" : "touch",
               output.c_str());
  return false;
}
}  // namespace

std::vector<OutputInfo> GnomeBackend::outputs() {
  MutterState st;
  if (!read_mutter(st)) return {};
  return mutter_outputs(st);
}

void GnomeBackend::map_touch(const std::string& output, const std::string& touch_dev) {
  gnome_map(false, output, session_device_id(0xd701, touch_dev));
}

bool GnomeBackend::map_pen(const std::string& output, const std::string& pen_dev) {
  // The injector derives the pen's id from the TOUCH name; the pen device is "<touch>-pen".
  std::string touch = pen_dev;
  if (touch.size() > 4 && touch.compare(touch.size() - 4, 4, "-pen") == 0)
    touch.resize(touch.size() - 4);
  return gnome_map(true, output, session_device_id(0xd704, touch));
}

bool GnomeBackend::apply_layout(const std::string& evdi, LayoutMode mode) {
  // Mutter extends a new monitor to the right on its own; only mirror needs a change.
  if (mode != LayoutMode::Mirror || !safe_output_name(evdi)) return false;
  MutterState st;
  if (!read_mutter(st)) return false;
  const std::string cfg = mutter_mirror_config(st, evdi);
  if (cfg.empty()) {
    std::fprintf(stderr, "[gnome] mirror: %s and the primary share no resolution; "
                         "staying extended\n", evdi.c_str());
    return false;
  }
  return apply_mutter(st, cfg, "mirror");
}

bool GnomeBackend::place_output(const std::string& evdi, int x, int y) {
  if (!safe_output_name(evdi)) return false;
  MutterState st;
  if (!read_mutter(st)) return false;
  const std::string cfg = mutter_place_config(st, evdi, x, y);
  if (cfg.empty()) return false;
  return apply_mutter(st, cfg, "placement");
}

std::string GnomeBackend::find_output(uint32_t edid_serial) {
  if (edid_serial == 0) return {};
  MutterState st;
  if (!read_mutter(st)) return {};
  return mutter_find_by_serial(st, edid_serial);
}

// Mutter, on a quick reconnect, can keep the output "on" from the last session and never
// commit a mode to the new evdi connection. Switching it off and back on (temporary
// configs) makes it commit one. Only acts when Mutter lists the output as on.
bool GnomeBackend::revive_output(uint32_t edid_serial) {
  if (edid_serial == 0) return false;
  MutterState before;
  if (!read_mutter(before)) return false;
  const std::string conn = mutter_find_by_serial(before, edid_serial);
  if (conn.empty()) {
    std::fprintf(stderr, "[gnome] revive: GNOME doesn't list the droppix monitor yet\n");
    return false;
  }
  const std::string off = mutter_disable_config(before, conn);
  Rect at{};
  for (const auto& o : mutter_outputs(before))
    if (o.name == conn && o.enabled) at = o.geom;
  if (off.empty() || !safe_output_name(conn)) return false;
  if (!apply_mutter(before, off, "revive (off)")) return false;
  MutterState now;
  if (!read_mutter(now)) return false;
  MutterState on_state = before;    // the old layout, re-applied under the new serial
  on_state.serial = now.serial;
  return apply_mutter(on_state, mutter_place_config(before, conn, at.x, at.y), "revive (on)");
}

// ---- Sway (wlroots) ----

namespace {
std::string swaymsg(const std::string& args) {
  return capture("timeout 5 " + user_session_prefix() + "swaymsg " + args + " 2>&1");
}

bool sway_outputs(std::vector<SwayOutput>& outs) {
  const std::string out = swaymsg("-t get_outputs -r");
  if (parse_sway_outputs(out, outs)) return true;
  std::fprintf(stderr, "[sway] could not read outputs: %.200s\n", out.c_str());
  return false;
}

bool sway_map(const char* what, const std::string& output, const std::string& dev) {
  if (!safe_output_name(output) || !safe_output_name(dev)) return false;
  // The device appears a moment after the uinput create; retry for ~3 s like KWin's binder.
  for (int i = 0; i < 15; ++i) {
    const std::string id = sway_input_identifier(swaymsg("-t get_inputs -r"), dev);
    if (!id.empty()) {
      const std::string out = swaymsg("input '" + id + "' map_to_output " + output);
      const bool ok = out.find("\"success\": true") != std::string::npos ||
                      out.find("\"success\":true") != std::string::npos || out.empty();
      std::fprintf(stderr, "[sway] %s %s -> %s%s%s\n", what, id.c_str(), output.c_str(),
                   ok ? "" : ": ", ok ? "" : out.c_str());
      return ok;
    }
    usleep(200000);
  }
  std::fprintf(stderr, "[sway] %s device %s not found\n", what, dev.c_str());
  return false;
}
}  // namespace

std::vector<OutputInfo> SwayBackend::outputs() {
  std::vector<SwayOutput> outs;
  if (!sway_outputs(outs)) return {};
  std::vector<OutputInfo> r;
  for (auto& o : outs) r.push_back(o.info);
  return r;
}

std::string SwayBackend::find_output(uint32_t edid_serial) {
  std::vector<SwayOutput> outs;
  if (edid_serial == 0 || !sway_outputs(outs)) return {};
  return sway_find_by_serial(outs, edid_serial);
}

void SwayBackend::map_touch(const std::string& output, const std::string& touch_dev) {
  sway_map("touch", output, touch_dev);
}

bool SwayBackend::map_pen(const std::string& output, const std::string& pen_dev) {
  return sway_map("pen", output, pen_dev);
}

bool SwayBackend::apply_layout(const std::string& evdi, LayoutMode mode) {
  if (mode == LayoutMode::Mirror)
    std::fprintf(stderr, "[sway] mirror: Sway has no output mirroring; %s stays extended\n",
                 evdi.c_str());
  return false;
}

bool SwayBackend::place_output(const std::string& evdi, int x, int y) {
  std::vector<SwayOutput> outs;
  if (!sway_outputs(outs)) return false;
  const std::string cmd = sway_place_command(outs, evdi, x, y);
  if (cmd.empty()) return false;
  const std::string out = swaymsg("'" + cmd + "'");
  std::fprintf(stderr, "[sway] place %s at %d,%d: %s\n", evdi.c_str(), x, y, out.c_str());
  return true;
}

// Sway never modes an evdi output when wlroots has no render device for it: say how to fix.
bool SwayBackend::revive_output(uint32_t edid_serial) {
  (void)edid_serial;
  std::fprintf(stderr,
      "[sway] the droppix screen has no mode yet. If it never appears, start Sway with "
      "WLR_EVDI_RENDER_DEVICE=/dev/dri/renderD128 (your GPU's render node) in its environment\n");
  return false;
}

// ---- Hyprland ----

namespace {
std::string hyprctl(const std::string& args) {
  return capture("timeout 5 " + user_session_prefix() + "hyprctl " + args + " 2>&1");
}
bool hypr_monitors(std::vector<HyprMonitor>& mons) {
  const std::string out = hyprctl("-j monitors all");
  if (parse_hypr_monitors(out, mons)) return true;
  std::fprintf(stderr, "[hyprland] could not read monitors: %.200s\n", out.c_str());
  return false;
}
// A hyprlang config takes `keyword`; Hyprland 0.5x's Lua config (hyprland.lua) answers that
// with "unknown request" and takes the same change as Lua through `eval`.
bool hypr_set(const std::string& what, const std::string& key, const std::string& value,
              const std::string& lua) {
  std::string out = hyprctl("keyword '" + key + "' '" + value + "'");
  const char* via = "keyword";
  if (out == "unknown request" && !lua.empty()) {
    out = hyprctl("eval '" + lua + "'");
    via = "eval";
  }
  const bool ok = out == "ok";
  std::fprintf(stderr, "[hyprland] %s (%s): %s %s%s%s\n", what.c_str(), via, key.c_str(),
               value.c_str(), ok ? "" : ": ", ok ? "" : out.c_str());
  return ok;
}
bool hypr_map(const char* what, const std::string& output, const std::string& dev) {
  if (!safe_output_name(output) || !safe_output_name(dev)) return false;
  return hypr_set(what, "device[" + dev + "]:output", output, hypr_device_lua(dev, output));
}
}  // namespace

std::vector<OutputInfo> HyprlandBackend::outputs() {
  std::vector<HyprMonitor> mons;
  if (!hypr_monitors(mons)) return {};
  std::vector<OutputInfo> r;
  for (auto& m : mons) r.push_back(m.info);
  return r;
}
std::string HyprlandBackend::find_output(uint32_t edid_serial) {
  std::vector<HyprMonitor> mons;
  if (edid_serial == 0 || !hypr_monitors(mons)) return {};
  return hypr_find_by_serial(mons, edid_serial);
}
void HyprlandBackend::map_touch(const std::string& output, const std::string& touch_dev) {
  hypr_map("touch", output, touch_dev);
}
bool HyprlandBackend::map_pen(const std::string& output, const std::string& pen_dev) {
  return hypr_map("pen", output, pen_dev);
}
bool HyprlandBackend::apply_layout(const std::string& evdi, LayoutMode mode) {
  if (mode != LayoutMode::Mirror) return false;
  std::vector<HyprMonitor> mons;
  if (!hypr_monitors(mons)) return false;
  std::string primary;
  for (const auto& m : mons)
    if (m.info.name != evdi && m.info.enabled && (primary.empty() || m.info.primary)) primary = m.info.name;
  const std::string rule = hypr_mirror_rule(mons, evdi, primary);
  return !rule.empty() && hypr_set("mirror", "monitor", rule, hypr_mirror_lua(mons, evdi, primary));
}
bool HyprlandBackend::place_output(const std::string& evdi, int x, int y) {
  std::vector<HyprMonitor> mons;
  if (!hypr_monitors(mons)) return false;
  const std::string rule = hypr_place_rule(mons, evdi, x, y);
  return !rule.empty() && hypr_set("placement", "monitor", rule, hypr_place_lua(mons, evdi, x, y));
}

void GenericBackend::map_touch(const std::string& output, const std::string& touch_dev) {
  (void)output; (void)touch_dev;
  std::fprintf(stderr, "input: touch-to-output mapping not supported on this desktop yet; "
                       "display works, touch not bound\n");
}

BackendKind select_backend_kind(const std::string& xdg_current_desktop, bool has_kscreen,
                                bool x11_session, bool has_x11_tools, bool has_mutter,
                                bool has_sway, bool has_hypr) {
  std::string d;
  for (char c : xdg_current_desktop) d.push_back(static_cast<char>(std::tolower((unsigned char)c)));
  const bool gnome = d.find("gnome") != std::string::npos || has_mutter;
  const bool sway = d.find("sway") != std::string::npos || has_sway;
  const bool hypr = d.find("hyprland") != std::string::npos || has_hypr;
  if (d.find("kde") != std::string::npos || d.find("plasma") != std::string::npos)
    return BackendKind::KWin;
  // A live Wayland compositor wins over "kscreen-doctor happens to be installed": under
  // pkexec the desktop name is gone, and a GNOME/Sway/Hyprland box with Plasma's tools
  // installed was driven as KWin.
  if (!x11_session) {
    if (gnome) return BackendKind::Gnome;
    if (sway) return BackendKind::Sway;
    if (hypr) return BackendKind::Hyprland;
  }
  if (d.empty() && has_kscreen) return BackendKind::KWin;
  if (x11_session && has_x11_tools) return BackendKind::X11;
  if (gnome) return BackendKind::Gnome;
  if (sway) return BackendKind::Sway;
  if (hypr) return BackendKind::Hyprland;
  return BackendKind::Generic;
}

uint16_t session_device_id(uint16_t base, const std::string& touch_name) {
  size_t i = touch_name.size();
  while (i > 0 && std::isdigit(static_cast<unsigned char>(touch_name[i - 1]))) --i;
  if (i == touch_name.size() || touch_name.size() - i > 9) return base;
  const unsigned long n = std::stoul(touch_name.substr(i));
  return static_cast<uint16_t>(base + ((n % 255) + 1) * 0x10);
}

std::string layout_command(BackendKind kind, const std::string& evdi,
                           const std::string& primary, int primary_id, LayoutMode mode) {
  if (!safe_output_name(evdi) || !safe_output_name(primary)) return {};
  switch (kind) {
    case BackendKind::KWin:
      return mode == LayoutMode::Mirror
        ? "kscreen-doctor \"output." + evdi + ".replicationSource." + std::to_string(primary_id) + "\""
        : "kscreen-doctor \"output." + evdi + ".replicationSource.0\"";
    case BackendKind::X11:
      return mode == LayoutMode::Mirror
        ? "xrandr --output " + evdi + " --same-as " + primary
        : "xrandr --output " + evdi + " --auto --right-of " + primary;
    case BackendKind::Generic: default:
      return {};
  }
}

GridPoint grid_position(int col, int row, int own_w, int own_h, int ax, int ay) {
  return { ax + col * own_w, ay + row * own_h };
}

std::string place_command(BackendKind kind, const std::string& evdi, int x, int y) {
  if (!safe_output_name(evdi)) return {};
  switch (kind) {
    case BackendKind::X11:  return "xrandr --output " + evdi + " --pos " + std::to_string(x) + "x" + std::to_string(y);
    case BackendKind::KWin: return "kscreen-doctor \"output." + evdi + ".position." + std::to_string(x) + "," + std::to_string(y) + "\"";
    case BackendKind::Generic: default: return {};
  }
}

std::shared_ptr<DesktopBackend> make_desktop_backend() {
  // Detect once per process: the daemon is reconstructed every session (orientation
  // flips, reconnects), and the backends are stateless, so one shared instance is safe
  // across sessions (incl. concurrent multi-monitor) and avoids re-forking `command -v`
  // + re-logging the "desktop backend:" line on every session.
  static std::shared_ptr<DesktopBackend> cached = []{
    const char* xdg = std::getenv("XDG_CURRENT_DESKTOP");
    const std::string desktop = xdg ? xdg : "";
    const bool has_kscreen = std::system("command -v kscreen-doctor >/dev/null 2>&1") == 0;
    // Is the user's session X11? XDG_SESSION_TYPE when present (plain user run); under
    // pkexec/sudo that env is gone, so probe sockets: a Wayland socket in the invoking
    // user's runtime dir means Wayland (XWayland also creates the X socket, so check
    // Wayland FIRST), otherwise the X socket means a real X11 session.
    const bool x11_session = []{
      const char* st = std::getenv("XDG_SESSION_TYPE");
      if (st && *st) return std::string(st) == "x11";
      const char* uid = std::getenv("PKEXEC_UID");
      if (!uid || !*uid) uid = std::getenv("SUDO_UID");
      // Any live wayland-N, not just wayland-0: Sway/Plasma often run on wayland-1, and
      // XWayland's X0 then made droppix pick the X11 backend (xinput can't see uinput there).
      if (uid && *uid && has_live_wayland_socket(list_dir("/run/user/" + std::string(uid))))
        return false;
      return access("/tmp/.X11-unix/X0", F_OK) == 0;
    }();
    const bool has_x11_tools =
        std::system("command -v xrandr >/dev/null 2>&1 && command -v xinput >/dev/null 2>&1") == 0;
    // Under pkexec XDG_CURRENT_DESKTOP is gone: ask the user's session bus whether Mutter's
    // display service is there (GNOME and its derivatives: Ubuntu, Zorin, Pop 22.04, ...).
    const bool has_mutter =
        capture("timeout 3 " + user_session_prefix() +
                "gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus "
                "--method org.freedesktop.DBus.NameHasOwner org.gnome.Mutter.DisplayConfig 2>/dev/null")
            .find("true") != std::string::npos;
    std::shared_ptr<DesktopBackend> b;
    const bool has_sway = [&]{
      const char* uid = std::getenv("PKEXEC_UID");
      if (!uid || !*uid) uid = std::getenv("SUDO_UID");
      if (uid && *uid) return !discover_sway_socket("/run/user/" + std::string(uid), std::atol(uid)).empty();
      const char* s = std::getenv("SWAYSOCK");
      return s && *s;
    }();
    const bool has_hypr = [&]{
      const char* uid = std::getenv("PKEXEC_UID");
      if (!uid || !*uid) uid = std::getenv("SUDO_UID");
      if (uid && *uid) return !discover_hypr_instance("/run/user/" + std::string(uid)).empty();
      const char* s = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
      return s && *s;
    }();
    switch (select_backend_kind(desktop, has_kscreen, x11_session, has_x11_tools, has_mutter,
                                has_sway, has_hypr)) {
      case BackendKind::KWin:  b = std::make_shared<KWinBackend>();  break;
      case BackendKind::X11:   b = std::make_shared<X11Backend>();   break;
      case BackendKind::Gnome: b = std::make_shared<GnomeBackend>(); break;
      case BackendKind::Sway:  b = std::make_shared<SwayBackend>();  break;
      case BackendKind::Hyprland: b = std::make_shared<HyprlandBackend>(); break;
      default:                b = std::make_shared<GenericBackend>();
    }
    std::fprintf(stderr, "desktop backend: %s\n", b->name());
    return b;
  }();
  return cached;
}

}  // namespace droppix
