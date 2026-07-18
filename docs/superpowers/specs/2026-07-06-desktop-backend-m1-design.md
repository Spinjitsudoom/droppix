# Cross-Desktop M1: `DesktopBackend` Abstraction — Design

**Status:** Shipped on master (plus X11 backend beyond original M1).
**Branch:** `feat/desktop-backend-m1` (off `master`)
**Parent roadmap:** `docs/superpowers/specs/2026-07-05-cross-desktop-portability-design.md` (approved).

## Goal

Relocate the two KWin/KDE-specific host operations behind one `DesktopBackend`
interface, add runtime desktop detection, and provide a graceful-degradation
fallback backend — so droppix runs on any Wayland compositor (display-only
where the compositor isn't yet supported), full-featured on KDE, with **zero
behavior change on KDE**. This is the foundation the Sway (M2) and GNOME (M3)
backends plug into. **Host-only; no Android/wire/protocol change.**

## Scope

**In scope (M1):**
- A `DesktopBackend` interface with two operations: `outputs()` (enumerate
  compositor outputs) and `map_touch()` (bind a touch device to an output).
- `KWinBackend` — today's exact behavior (`kscreen-doctor -o` + KWin DBus),
  relocated behind the interface. No behavior change on KDE.
- `GenericBackend` — graceful degradation for unknown compositors: `outputs()`
  returns empty, `map_touch()` logs once and no-ops. The display still streams
  (evdi is compositor-driven); only touch-to-output mapping is skipped.
- A **pure, unit-tested** backend selector + a `make_desktop_backend()` factory
  that auto-detects the desktop.
- `StreamDaemon` rewired to call the backend instead of the removed statics.

**Out of scope (later milestones / YAGNI):**
- The Sway backend (M2) and GNOME backend (M3).
- `WAYLAND_DISPLAY` real-socket discovery + `SWAYSOCK` (needed by M2; the KDE
  path keeps its working `wayland-0` default so M1 stays behavior-preserving).
- X11, output positioning/rotation (droppix never sets these), any GUI change.

## Context (current code, verified 2026-07-06)

All coupling lives in `host/src/stream_daemon.cpp` as three file-static helpers:
- `user_cmd_prefix()` (lines 23-35) — builds a `runuser -u <user> -- env …`
  prefix so root can run session commands as the user. Used by `run_kscreen`,
  `bind_touch_to_output`, **and** the audio path (`audio.start(user_cmd_prefix())`,
  line 204). Hardcodes `WAYLAND_DISPLAY=wayland-0` (fine on KDE).
- `run_kscreen()` (lines 37-47) — `kscreen-doctor -o` via the prefix, returns raw text.
- `bind_touch_to_output(output, touch)` (lines 63-91) — KWin DBus
  `org.kde.KWin.InputDevice` shell command; called in a detached thread (line 178).
- `safe_output_name()` (lines 51-55) — validates connector-id strings before shell
  interpolation.

Consumers in `run_until`:
- `parse_kscreen_outputs(run_kscreen())` at lines 98 (before) and 140 (after) →
  `std::vector<OutputInfo>`. `parse_kscreen_outputs` and the `select_new_output` /
  `select_droppix` selectors live in `host/src/monitor_geometry.{h,cpp}` and are
  **already pure and unit-tested** — they stay put and are reused by `KWinBackend`.
- `std::thread(bind_touch_to_output, droppix.name, cfg_.touch_name).detach()` (line 178).
- `audio.start(user_cmd_prefix())` (line 204).

## Architecture

New unit `host/src/desktop_backend.{h,cpp}` owns the compositor coupling:

```cpp
namespace droppix {

// Session-command prefix so the root streamer can run user-session tools (kscreen,
// KWin DBus, pw-record) AS THE INVOKING USER. Relocated verbatim from stream_daemon
// (unchanged: keeps WAYLAND_DISPLAY=wayland-0 — real-socket discovery is M2's job).
std::string user_session_prefix();

// Per-desktop operations droppix needs beyond creating the evdi output. Everything
// else (compositing the virtual display, encode, stream) is compositor-agnostic.
struct DesktopBackend {
  virtual ~DesktopBackend() = default;
  virtual const char* name() const = 0;                  // for logs: "kwin" / "generic"
  virtual std::vector<OutputInfo> outputs() = 0;         // enabled outputs w/ geometry
  virtual void map_touch(const std::string& output,
                         const std::string& touch_dev) = 0;  // best-effort; may no-op
};

// KDE: today's behavior, relocated. outputs() = kscreen-doctor -o + parse_kscreen_outputs;
// map_touch() = the KWin InputDevice DBus command. Uses user_session_prefix().
class KWinBackend : public DesktopBackend { … };

// Unknown/unsupported compositor: display still works (evdi is compositor-driven);
// outputs() returns {} and map_touch() logs once ("touch mapping not supported on
// this desktop yet; display works, touch not bound") and no-ops.
class GenericBackend : public DesktopBackend { … };

enum class BackendKind { KWin, Generic };

// PURE + unit-tested. KDE (XDG_CURRENT_DESKTOP contains "KDE"/"plasma", case-insensitive)
// OR (desktop unknown AND kscreen-doctor present) -> KWin; otherwise Generic. The
// root streamer's env usually lacks XDG_CURRENT_DESKTOP (pkexec strips it), so tool
// presence is the primary KDE signal today; M2/M3 extend this selector.
BackendKind select_backend_kind(const std::string& xdg_current_desktop, bool has_kscreen);

// Gathers the signals (env + `command -v kscreen-doctor`), calls the selector,
// constructs the backend. Logs the chosen backend name.
std::shared_ptr<DesktopBackend> make_desktop_backend();

}  // namespace droppix
```

`StreamDaemon` gains a `std::shared_ptr<DesktopBackend> desktop_` (created once via
`make_desktop_backend()`), and `run_until`:
- replaces `parse_kscreen_outputs(run_kscreen())` → `desktop_->outputs()` (both sites);
- replaces the detached `bind_touch_to_output(...)` thread with one that captures a
  **copy of the `shared_ptr`** and calls `desktop_->map_touch(name, touch)` — so the
  backend outlives the daemon if the ~10 s bind thread is still running at teardown
  (today's free-function call has no such lifetime coupling; the shared_ptr preserves
  that safety);
- replaces `audio.start(user_cmd_prefix())` → `audio.start(user_session_prefix())`.

The three statics (`user_cmd_prefix`, `run_kscreen`, `bind_touch_to_output`,
`safe_output_name`) are removed from `stream_daemon.cpp` (moved into
`desktop_backend.cpp`; `safe_output_name` becomes a file-static there).

## Data flow (unchanged on KDE)

1. `StreamDaemon` constructs `desktop_ = make_desktop_backend()` → `KWinBackend` on KDE.
2. `run_until` snapshots `desktop_->outputs()` before/after the evdi monitor appears,
   diffs them (`select_new_output`) to identify the droppix output — same as today.
3. If touch is enabled and the output was identified, a detached thread calls
   `desktop_->map_touch(droppix.name, touch_name)` — same KWin DBus command as today.
4. On a non-KDE compositor, `GenericBackend::outputs()` returns `{}` → the existing
   "could not identify the droppix output" path fires → touch is skipped, **display
   still streams**. Graceful degradation with no new code path.

## Error handling / degradation

| Situation | Behavior |
|---|---|
| KDE (kscreen-doctor present) | `KWinBackend`; identical to today. |
| Unknown compositor | `GenericBackend`; `outputs()` empty → touch mapping skipped with the existing warning; display works. |
| `map_touch` on `GenericBackend` | Logs once, no-op (never invoked in practice since `outputs()` is empty → `have_output` false, but safe if called). |
| kscreen-doctor missing on a "KDE" env | Selector still picks KWin from the desktop string; `outputs()` returns `{}` (kscreen fails) → degrades like Generic. |

## Testing

**New `host/tests/test_desktop_backend.cpp` (pure selector — primary):**
- `select_backend_kind("KDE", false)` → `KWin` (desktop string wins even w/o tool).
- `select_backend_kind("plasma", false)` → `KWin` (case-insensitive substring).
- `select_backend_kind("", true)` → `KWin` (unknown desktop + kscreen present).
- `select_backend_kind("", false)` → `Generic` (nothing indicates KDE).
- `select_backend_kind("GNOME", false)` → `Generic` (not KDE, no backend yet).
- `select_backend_kind("sway", true)` → `Generic` (Sway isn't KWin even if kscreen
  happens to be installed — desktop string is not KDE).

**Reused (already green):** `test_monitor_geometry.cpp` covers
`parse_kscreen_outputs` + the selectors that `KWinBackend::outputs()` composes.

**Manual (KDE, run by the operator):** start a stream on KDE, confirm the host log
shows `desktop backend: kwin`, the second monitor appears, and touch still maps to
it (no regression) — i.e. M1 changed nothing user-visible on KDE.

`KWinBackend`/`GenericBackend` themselves are thin shells over external tools and are
not unit-tested (they'd require a live compositor); the selector holds the testable
logic, and the KDE manual check covers the relocated behavior end-to-end.

## File structure summary

| File | Responsibility |
|---|---|
| `host/src/desktop_backend.h` (new) | Interface, `BackendKind`, pure selector, factory, `user_session_prefix` decls |
| `host/src/desktop_backend.cpp` (new) | `KWinBackend`, `GenericBackend`, `select_backend_kind`, `make_desktop_backend`, moved helpers |
| `host/tests/test_desktop_backend.cpp` (new) | Pure selector tests |
| `host/src/stream_daemon.{h,cpp}` (mod) | Remove 3 statics; hold `shared_ptr<DesktopBackend> desktop_`; call through it |
| `host/CMakeLists.txt` (mod) | Add `src/desktop_backend.cpp` to `droppix_core`; `tests/test_desktop_backend.cpp` to `droppix_tests` |

## Companion deliverable: M0 GNOME validation procedure

M1 is ungated, but the **GNOME backend (M3) is gated on M0** — proving GNOME
Wayland adopts an evdi output unmodified. Since M0 needs a live GNOME session
(operator-run, not automatable from here), this spec ships a short M0 procedure
the operator runs when convenient; its result decides whether M3 proceeds:

1. Boot a **GNOME Wayland** session (Fedora/Ubuntu GNOME, a VM, or a spare box).
2. Install evdi + run the droppix streamer with `--test-pattern` first, then evdi.
3. **Observe:** does a new display appear in GNOME Settings → Displays, and does the
   test pattern / desktop render on the tablet? Record: evdi module loads? output
   appears? mode set? touch (expected to be unmapped pre-M3)?
4. **Go/no-go:** if the display appears unmodified → M3 is viable (build the Mutter
   `outputs()` + touch backend). If not → M3 grows (evdi build/DisplayLink daemon)
   or drops; report findings first.

(This procedure is documentation for the operator; it is not built or tested here.)
