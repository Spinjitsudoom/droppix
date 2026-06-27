#include "stream_daemon.h"
#include "stat_accumulator.h"
#include "stats_json.h"
#include "input_injector.h"
#include "monitor_geometry.h"
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <pwd.h>
#include <string>
#include <thread>

namespace droppix {

// The streamer runs as root (pkexec/sudo) and can't see the user's Wayland/KWin
// session, so any session command (kscreen-doctor, KWin DBus) must run AS THE
// invoking user with a reconstructed session env. Returns a command prefix like
// "runuser -u name -- env XDG_RUNTIME_DIR=... " (empty if already a user session).
// Uses runuser (not sudo): sudo often refuses without a tty, which pkexec lacks.
static std::string user_cmd_prefix() {
  const char* uid = std::getenv("PKEXEC_UID");
  if (!uid || !*uid) uid = std::getenv("SUDO_UID");
  if (!uid || !*uid) return "env ";  // already in a user session
  const std::string u(uid);
  const std::string env =
      "env XDG_RUNTIME_DIR=/run/user/" + u + " "
      "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/" + u + "/bus "
      "WAYLAND_DISPLAY=wayland-0 ";
  struct passwd* pw = getpwuid(static_cast<uid_t>(std::atoi(u.c_str())));
  if (pw && pw->pw_name) return std::string("runuser -u ") + pw->pw_name + " -- " + env;
  return "sudo -u '#" + u + "' " + env;
}

static std::string run_kscreen() {
  std::string out;
  std::string cmd = "timeout 3 " + user_cmd_prefix() + "kscreen-doctor -o 2>/dev/null";
  FILE* p = popen(cmd.c_str(), "r");
  if (!p) return out;
  char buf[4096]; size_t n;
  while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
  pclose(p);
  std::fprintf(stderr, "input: kscreen query returned %zu bytes\n", out.size());
  return out;
}

// Output names are short connector ids (DP-3, HDMI-A-3, ...); reject anything else
// so the name can be safely interpolated into the bind shell command.
static bool safe_output_name(const std::string& s) {
  if (s.empty() || s.size() > 64) return false;
  for (char c : s) if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') return false;
  return true;
}

// Map the droppix-touch device onto the droppix output via KWin's per-device DBus
// properties, so the touchscreen's full range maps to that monitor instead of KWin's
// default output. Sets BOTH mapToWorkspace=false (else the device spans the whole
// desktop and outputName is ignored) and outputName=<droppix>. Logs KWin's before/
// after state to stderr (host log) so a single run shows exactly what KWin did.
// Retries while KWin registers the new uinput device; runs detached / timeout-guarded.
static void bind_touch_to_output(std::string output_name) {
  if (!safe_output_name(output_name)) return;
  // Properties must be read/written via org.freedesktop.DBus.Properties (the qdbus
  // shorthand "Interface.prop value" silently errors with UnknownInterface on these
  // objects). G/S = Get/Set helpers. inner uses ONLY double quotes so it can be wrapped
  // in single quotes for the outer shell — keeping $(...)/$VAR unexpanded until the
  // inner sh (running as the user) evaluates them.
  const std::string inner =
      "QD=; for q in qdbus6 qdbus-qt6 qdbus; do command -v \"$q\" >/dev/null 2>&1 && QD=$q && break; done; "
      "[ -z \"$QD\" ] && { echo \"[touch-bind] no qdbus available\" >&2; exit 0; }; "
      "I=org.kde.KWin.InputDevice; PG=org.freedesktop.DBus.Properties.Get; PS=org.freedesktop.DBus.Properties.Set; "
      "for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do "
      "for d in $(\"$QD\" org.kde.KWin /org/kde/KWin/InputDevice "
      "org.kde.KWin.InputDeviceManager.ListTouch 2>/dev/null); do "
      "P=/org/kde/KWin/InputDevice/$d; "
      "n=$(\"$QD\" org.kde.KWin \"$P\" $PG $I name 2>/dev/null); "
      "if [ \"$n\" = droppix-touch ]; then "
      "echo \"[touch-bind] found droppix-touch ($d) before mapToWorkspace=$(\"$QD\" org.kde.KWin \"$P\" $PG $I mapToWorkspace 2>/dev/null) outputName=[$(\"$QD\" org.kde.KWin \"$P\" $PG $I outputName 2>/dev/null)] target=" +
      output_name + "\" >&2; "
      "\"$QD\" org.kde.KWin \"$P\" $PS $I mapToWorkspace false 2>&1 | sed \"s/^/[touch-bind] set mapToWorkspace: /\" >&2; "
      "\"$QD\" org.kde.KWin \"$P\" $PS $I outputName " + output_name +
      " 2>&1 | sed \"s/^/[touch-bind] set outputName: /\" >&2; "
      "echo \"[touch-bind] after mapToWorkspace=$(\"$QD\" org.kde.KWin \"$P\" $PG $I mapToWorkspace 2>/dev/null) outputName=[$(\"$QD\" org.kde.KWin \"$P\" $PG $I outputName 2>/dev/null)]\" >&2; "
      "exit 0; fi; done; sleep 0.2; done; "
      "echo \"[touch-bind] droppix-touch not found via ListTouch after retries\" >&2";
  std::string cmd = "timeout 10 " + user_cmd_prefix() + "sh -c '" + inner + "'";
  std::system(cmd.c_str());
}

// Rotate the droppix output via KWin (kscreen-doctor), run as the user. The scanout
// stays at the mode's WxH (so capture/encoder are unaffected); only the rendered
// content is rotated, and KWin auto-applies the same rotation to the bound touch
// device. degrees: 90->left, 180->inverted, 270->right (0 => no-op).
static void apply_rotation(const std::string& output_name, int degrees) {
  if (degrees == 0 || !safe_output_name(output_name)) return;
  const char* rot = degrees == 90 ? "left" : degrees == 180 ? "inverted"
                  : degrees == 270 ? "right" : nullptr;
  if (!rot) return;
  std::string cmd = "timeout 5 " + user_cmd_prefix() + "kscreen-doctor output." +
                    output_name + ".rotation." + rot + " >/dev/null 2>&1";
  std::system(cmd.c_str());
  std::fprintf(stderr, "orientation: rotated output %s -> %s (%d deg)\n",
               output_name.c_str(), rot, degrees);
}

bool StreamDaemon::run_until(const volatile std::sig_atomic_t& stop, int max_frames) {
  // For touch/orientation we need the droppix output's NAME: snapshot the outputs
  // BEFORE the source creates its monitor, so we can identify it as the one that
  // newly appears afterwards.
  const bool need_output = cfg_.touch || cfg_.orientation != 0;
  std::vector<OutputInfo> before_outputs;
  if (need_output) {
    before_outputs = parse_kscreen_outputs(run_kscreen());
  }

  int w = 0, h = 0;
  if (!src_.start(w, h)) { std::fprintf(stderr, "source start failed\n"); return false; }
  std::fprintf(stderr, "source %dx%d\n", w, h);

  if (!tx_.accept_client(60000)) { std::fprintf(stderr, "no client\n"); return false; }
  uint32_t cver, cw, ch, density;
  if (!tx_.read_hello(cver, cw, ch, density, 10000)) { std::fprintf(stderr, "no HELLO\n"); return false; }
  std::fprintf(stderr, "client HELLO v%u %ux%u\n", cver, cw, ch);

  if (!enc_.open(w, h, cfg_.fps, cfg_.bitrate_kbps)) { std::fprintf(stderr, "encoder open failed\n"); return false; }
  if (!tx_.send_config(w, h, cfg_.fps, enc_.extradata())) return false;

  // Identify the droppix output (needed for orientation and/or touch): prefer the
  // newly-appeared one (unambiguous), else fall back to a size match.
  OutputInfo droppix;
  bool found_output = false;
  if (need_output) {
    auto after = parse_kscreen_outputs(run_kscreen());
    found_output = select_new_output(before_outputs, after, droppix) ||
                   select_droppix(after, w, h, droppix);
    if (!found_output)
      std::fprintf(stderr, "warning: could not identify the droppix output\n");
  }

  // Orientation (--orientation 90/180/270): rotate the droppix output via KWin.
  if (cfg_.orientation != 0 && found_output && safe_output_name(droppix.name)) {
    apply_rotation(droppix.name, cfg_.orientation);
  }

  // Touch input (opt-in via --touch): inject the tablet's touches via a uinput
  // touchscreen (needs root) that KWin binds to the droppix output. OFF by default
  // so a geometry-query or uinput issue can never affect the display-only path.
  InputInjector injector;
  tx_.set_input_handler(nullptr);  // drop any handler from a prior session (its injector is gone)
  if (cfg_.touch) {
    if (injector.open()) {
      tx_.set_input_handler([&injector](uint8_t a, uint16_t x, uint16_t y) {
        injector.inject(a, x, y);
      });
      if (found_output && safe_output_name(droppix.name)) {
        std::fprintf(stderr, "input: binding touch -> output %s (%dx%d)\n",
                     droppix.name.c_str(), droppix.geom.w, droppix.geom.h);
        std::thread(bind_touch_to_output, droppix.name).detach();
      } else {
        std::fprintf(stderr, "input: could not identify droppix output; touch may land "
                     "on the wrong monitor (map 'droppix-touch' in System Settings > Touch Screen)\n");
      }
    } else {
      std::fprintf(stderr, "input: uinput unavailable (need root); input disabled\n");
    }
  }

  auto t0 = std::chrono::steady_clock::now();
  int sent = 0;
  StatAccumulator encode_ms, frame_kb;
  int frames_since_report = 0;
  auto last_report = std::chrono::steady_clock::now();
  // With touch on, poll the loop tightly so incoming touch is handled promptly
  // instead of being gated by the up-to-1s damage-driven frame wait.
  const int frame_timeout = cfg_.touch ? 8 : 1000;
  while (!stop && tx_.connected()) {
    Frame f = src_.next(frame_timeout);
    if (!f.valid) { tx_.poll_control(); continue; }
    int64_t pts_us = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::steady_clock::now() - t0).count();
    auto enc_t0 = std::chrono::steady_clock::now();
    auto packets = enc_.encode(f, pts_us);
    double enc_ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - enc_t0).count();
    encode_ms.add(enc_ms);
    for (auto& pkt : packets) {
      frame_kb.add(pkt.data.size() / 1024.0);
      if (!tx_.send_video(pkt.pts_us, pkt.keyframe, pkt.data)) break;
      ++sent;
    }
    ++frames_since_report;

    auto now = std::chrono::steady_clock::now();
    double elapsed_s = std::chrono::duration<double>(now - last_report).count();
    if (elapsed_s >= 1.0) {
      if (cfg_.stats_json) {
        std::fprintf(stderr, "%s\n", format_stats_json(
            encode_ms.avg(), encode_ms.peak(), frames_since_report / elapsed_s,
            frame_kb.avg(), frame_kb.peak(), tx_.connected()).c_str());
      } else {
        std::fprintf(stderr,
            "stats: encode avg %.1f ms peak %.1f ms | fps %.1f | frame avg %.1f KB peak %.1f KB\n",
            encode_ms.avg(), encode_ms.peak(), frames_since_report / elapsed_s,
            frame_kb.avg(), frame_kb.peak());
      }
      encode_ms.reset(); frame_kb.reset(); frames_since_report = 0; last_report = now;
    }

    tx_.poll_control();
    if (max_frames > 0 && sent >= max_frames) break;
  }
  for (auto& pkt : enc_.flush()) tx_.send_video(pkt.pts_us, pkt.keyframe, pkt.data);
  std::fprintf(stderr, "sent %d video packets\n", sent);
  return true;
}

}  // namespace droppix
