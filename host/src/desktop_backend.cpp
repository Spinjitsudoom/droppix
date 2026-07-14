#include "desktop_backend.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>

namespace droppix {

std::string user_session_prefix() {
  const char* uid = std::getenv("PKEXEC_UID");
  if (!uid || !*uid) uid = std::getenv("SUDO_UID");
  if (!uid || !*uid) return "env ";  // already in a user session
  const std::string u(uid);
  const std::string env =
      "env XDG_RUNTIME_DIR=/run/user/" + u + " "
      "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/" + u + "/bus "
      "WAYLAND_DISPLAY=wayland-0 "
      "DISPLAY=:0 ";   // X11 tools (xrandr/xinput); same "common default" shortcut as wayland-0
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
      "for d in $(\"$QD\" org.kde.KWin /org/kde/KWin/InputDevice "
      "org.kde.KWin.InputDeviceManager.ListTouch 2>/dev/null); do "
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
      "echo \"[touch-bind] droppix-touch not found via ListTouch after retries\" >&2";
  std::string cmd = "timeout 10 " + user_session_prefix() + "sh -c '" + inner + "'";
  std::system(cmd.c_str());
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

// Pin the droppix touch device to the droppix output via `xinput map-to-output`.
// Retries while X registers the new uinput device (mirrors KWinBackend's wait on
// ListTouch). Both names pass safe_output_name, so bare interpolation is safe.
void X11Backend::map_touch(const std::string& output_name, const std::string& touch_name) {
  if (!safe_output_name(output_name)) return;
  if (!safe_output_name(touch_name)) return;
  // map-to-output bakes the output's CURRENT geometry into the device's transform matrix,
  // so binding before adopt_output's placement has settled in X pins touch to the output's
  // stale position (wrong screen). Once the device appears, RE-APPLY the mapping until the
  // target output's geometry stops changing (idempotent + cheap), so the final binding
  // reflects the settled layout.
  const std::string inner =
      "N=" + output_name + "; T=" + touch_name + "; "
      "geom() { xrandr --query 2>/dev/null | awk -v n=\"$N\" \"\\$1==n{"
      "for(i=1;i<=NF;i++) if(\\$i ~ /[0-9]+x[0-9]+\\+[0-9]+\\+[0-9]+/){print \\$i; exit}}\"; }; "
      "found=0; last=; stable=0; "
      "for i in $(seq 1 25); do "
      "if xinput list --name-only 2>/dev/null | grep -Fxq \"$T\"; then found=1; "
      "xinput map-to-output \"$T\" \"$N\" 2>&1 | sed \"s/^/[touch-bind] map-to-output: /\" >&2; "
      "g=$(geom); "
      "if [ -n \"$g\" ] && [ \"$g\" = \"$last\" ]; then stable=$((stable+1)); "
      "[ \"$stable\" -ge 3 ] && { echo \"[touch-bind] mapped $T -> $N @ $g\" >&2; exit 0; }; "
      "else stable=0; fi; last=$g; "
      "fi; sleep 0.2; done; "
      "[ \"$found\" = 1 ] && echo \"[touch-bind] mapped $T -> $N (layout unsettled)\" >&2 || "
      "echo \"[touch-bind] $T not found via xinput after retries\" >&2";
  std::string cmd = "timeout 10 " + user_session_prefix() + "sh -c '" + inner + "'";
  std::system(cmd.c_str());
}

// Adopt the just-appeared droppix output on X11. Xorg hot-adds the evdi card as a
// second GPU but nothing renders into it until it is linked to the primary GPU as a
// reverse-PRIME sink; and the desktop's auto-reconfigure can scramble the layout
// (black screens). So: link the providers, place the droppix output right of the
// primary, and re-assert the primary — an explicit, sane layout.
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

// Unsupported compositor: logs a warning and no-ops (display still works via evdi).
void GenericBackend::map_touch(const std::string& output, const std::string& touch_dev) {
  (void)output; (void)touch_dev;
  std::fprintf(stderr, "input: touch-to-output mapping not supported on this desktop yet; "
                       "display works, touch not bound\n");
}

BackendKind select_backend_kind(const std::string& xdg_current_desktop, bool has_kscreen,
                                bool x11_session, bool has_x11_tools) {
  std::string d;
  for (char c : xdg_current_desktop) d.push_back(static_cast<char>(std::tolower((unsigned char)c)));
  if (d.find("kde") != std::string::npos || d.find("plasma") != std::string::npos)
    return BackendKind::KWin;
  if (d.empty() && has_kscreen) return BackendKind::KWin;
  if (x11_session && has_x11_tools) return BackendKind::X11;
  return BackendKind::Generic;
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
      if (uid && *uid) {
        const std::string wl = "/run/user/" + std::string(uid) + "/wayland-0";
        if (access(wl.c_str(), F_OK) == 0) return false;
      }
      return access("/tmp/.X11-unix/X0", F_OK) == 0;
    }();
    const bool has_x11_tools =
        std::system("command -v xrandr >/dev/null 2>&1 && command -v xinput >/dev/null 2>&1") == 0;
    std::shared_ptr<DesktopBackend> b;
    switch (select_backend_kind(desktop, has_kscreen, x11_session, has_x11_tools)) {
      case BackendKind::KWin: b = std::make_shared<KWinBackend>(); break;
      case BackendKind::X11:  b = std::make_shared<X11Backend>();  break;
      default:                b = std::make_shared<GenericBackend>();
    }
    std::fprintf(stderr, "desktop backend: %s\n", b->name());
    return b;
  }();
  return cached;
}

}  // namespace droppix
