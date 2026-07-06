# Cross-Desktop M1: DesktopBackend Abstraction — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Relocate the two KWin-specific host operations behind a `DesktopBackend` interface (with a KDE backend + a graceful-degradation fallback and auto-detection), with zero behavior change on KDE.

**Architecture:** A new `host/src/desktop_backend.{h,cpp}` holds the interface, `KWinBackend` (today's `kscreen-doctor` + KWin-DBus behavior, relocated), `GenericBackend` (empty outputs / no-op touch for unknown compositors), a pure unit-tested selector, and a factory. `StreamDaemon` calls the backend instead of the removed file-statics.

**Tech Stack:** C++17, GoogleTest, POSIX (popen/runuser), reuses `monitor_geometry` (pure, already tested).

## Global Constraints

- **Host-only.** No Android/wire/protocol/GUI change.
- **Zero behavior change on KDE.** `KWinBackend` runs the exact same `kscreen-doctor -o` command and KWin-DBus touch-bind command as today; `user_session_prefix()` keeps `WAYLAND_DISPLAY=wayland-0` (real-socket discovery is M2, not M1).
- **Graceful degradation:** on a non-KDE compositor the display still streams (evdi is compositor-driven); `GenericBackend::outputs()` returns `{}` so the existing "could not identify the droppix output" path skips touch mapping.
- **Detection selector is pure and unit-tested.** `select_backend_kind(xdg_current_desktop, has_kscreen)`: contains "kde"/"plasma" (case-insensitive) OR (empty desktop AND kscreen present) → KWin; else Generic.
- **Lifetime:** the detached touch-bind thread captures a copy of a `shared_ptr<DesktopBackend>` so the backend outlives the daemon if the ~10 s bind is still running at teardown.
- **Build/test env:** edit in the CIFS tree `/var/mnt/nas/Projects/Spacedesk for linux`; build off-mount in the `droppix-dev` distrobox. Build+test: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . -j"$(nproc)" && ctest --output-on-failure -R <regex>'`.
- Existing suite (161 tests) stays green.

---

## File Structure

| File | Responsibility |
|---|---|
| `host/src/desktop_backend.h` (new) | Interface, `BackendKind`, pure `select_backend_kind`, `make_desktop_backend`, `user_session_prefix` decls |
| `host/src/desktop_backend.cpp` (new) | `KWinBackend`, `GenericBackend`, selector, factory, moved `user_session_prefix`/`safe_output_name` |
| `host/tests/test_desktop_backend.cpp` (new) | Pure selector tests |
| `host/src/stream_daemon.{h,cpp}` (mod) | Remove 3 statics; hold `shared_ptr<DesktopBackend> desktop_`; call through it |
| `host/CMakeLists.txt` (mod) | `src/desktop_backend.cpp` → `droppix_core`; `tests/test_desktop_backend.cpp` → `droppix_tests` |

---

### Task 1: `desktop_backend` unit (interface, backends, selector, factory) + selector tests

**Files:**
- Create: `host/src/desktop_backend.h`, `host/src/desktop_backend.cpp`, `host/tests/test_desktop_backend.cpp`
- Modify: `host/CMakeLists.txt`

**Interfaces:**
- Produces: `struct DesktopBackend`, `class KWinBackend`, `class GenericBackend`, `enum class BackendKind { KWin, Generic }`, `BackendKind select_backend_kind(const std::string&, bool)`, `std::shared_ptr<DesktopBackend> make_desktop_backend()`, `std::string user_session_prefix()`.
- This task does NOT touch `stream_daemon.cpp` (its file-statics remain and still drive behavior). The new unit compiles into `droppix_core` and is exercised only by the new test until Task 2 wires it in — so the suite stays green with no behavior change.

- [ ] **Step 1: Write the failing selector test**

Create `host/tests/test_desktop_backend.cpp`:

```cpp
#include "desktop_backend.h"
#include <gtest/gtest.h>

using namespace droppix;

TEST(DesktopBackend, KdeDesktopSelectsKWin) {
  EXPECT_EQ(select_backend_kind("KDE", false), BackendKind::KWin);
}
TEST(DesktopBackend, PlasmaDesktopSelectsKWinCaseInsensitive) {
  EXPECT_EQ(select_backend_kind("plasma", false), BackendKind::KWin);
  EXPECT_EQ(select_backend_kind("KDE:plasmawayland", false), BackendKind::KWin);
}
TEST(DesktopBackend, UnknownDesktopWithKscreenSelectsKWin) {
  EXPECT_EQ(select_backend_kind("", true), BackendKind::KWin);
}
TEST(DesktopBackend, UnknownDesktopNoToolSelectsGeneric) {
  EXPECT_EQ(select_backend_kind("", false), BackendKind::Generic);
}
TEST(DesktopBackend, GnomeSelectsGeneric) {
  EXPECT_EQ(select_backend_kind("GNOME", false), BackendKind::Generic);
}
TEST(DesktopBackend, NonKdeDesktopIgnoresKscreenPresence) {
  // A named non-KDE desktop is Generic even if kscreen-doctor happens to be installed;
  // the tool only promotes an UNKNOWN desktop.
  EXPECT_EQ(select_backend_kind("sway", true), BackendKind::Generic);
}
```

- [ ] **Step 2: Add `desktop_backend.h`**

Create `host/src/desktop_backend.h`:

```cpp
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "monitor_geometry.h"   // droppix::OutputInfo

namespace droppix {

// Session-command prefix so the root streamer can run user-session tools (kscreen,
// KWin DBus, pw-record) AS THE INVOKING USER via runuser + a reconstructed env.
// Returns "env " when already a user session. (Relocated from stream_daemon,
// unchanged: keeps WAYLAND_DISPLAY=wayland-0 — real-socket discovery is M2's job.)
std::string user_session_prefix();

// Per-desktop operations droppix needs beyond creating the evdi output. Compositing
// the virtual display, encode, and stream are compositor-agnostic and not here.
struct DesktopBackend {
  virtual ~DesktopBackend() = default;
  virtual const char* name() const = 0;                       // for logs: "kwin"/"generic"
  virtual std::vector<OutputInfo> outputs() = 0;              // enabled outputs w/ geometry
  virtual void map_touch(const std::string& output,
                         const std::string& touch_dev) = 0;   // best-effort; may no-op
};

// KDE Plasma: today's behavior, relocated (kscreen-doctor -o + KWin InputDevice DBus).
class KWinBackend : public DesktopBackend {
 public:
  const char* name() const override { return "kwin"; }
  std::vector<OutputInfo> outputs() override;
  void map_touch(const std::string& output, const std::string& touch_dev) override;
};

// Unknown/unsupported compositor: display still works (evdi is compositor-driven);
// outputs() returns {} and map_touch() logs and no-ops.
class GenericBackend : public DesktopBackend {
 public:
  const char* name() const override { return "generic"; }
  std::vector<OutputInfo> outputs() override { return {}; }
  void map_touch(const std::string& output, const std::string& touch_dev) override;
};

enum class BackendKind { KWin, Generic };

// PURE (unit-tested). "kde"/"plasma" in the desktop string (case-insensitive) OR
// (empty desktop AND kscreen-doctor present) -> KWin; otherwise Generic.
BackendKind select_backend_kind(const std::string& xdg_current_desktop, bool has_kscreen);

// Detect the desktop (env + `command -v kscreen-doctor`), pick the backend, log it.
std::shared_ptr<DesktopBackend> make_desktop_backend();

}  // namespace droppix
```

- [ ] **Step 3: Add `desktop_backend.cpp`** (moves the three statics verbatim behind the interface)

Create `host/src/desktop_backend.cpp`:

```cpp
#include "desktop_backend.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <pwd.h>

namespace droppix {

std::string user_session_prefix() {
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

namespace {
// Output names are short connector ids (DP-3, HDMI-A-3, ...); reject anything else
// so the name can be safely interpolated into the bind shell command.
bool safe_output_name(const std::string& s) {
  if (s.empty() || s.size() > 64) return false;
  for (char c : s) if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') return false;
  return true;
}
}  // namespace

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
  if (!safe_output_name(touch_name)) return;
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

void GenericBackend::map_touch(const std::string& output, const std::string& touch_dev) {
  (void)output; (void)touch_dev;
  std::fprintf(stderr, "input: touch-to-output mapping not supported on this desktop yet; "
                       "display works, touch not bound\n");
}

BackendKind select_backend_kind(const std::string& xdg_current_desktop, bool has_kscreen) {
  std::string d;
  for (char c : xdg_current_desktop) d.push_back(static_cast<char>(std::tolower((unsigned char)c)));
  if (d.find("kde") != std::string::npos || d.find("plasma") != std::string::npos)
    return BackendKind::KWin;
  if (d.empty() && has_kscreen) return BackendKind::KWin;
  return BackendKind::Generic;
}

std::shared_ptr<DesktopBackend> make_desktop_backend() {
  const char* xdg = std::getenv("XDG_CURRENT_DESKTOP");
  const std::string desktop = xdg ? xdg : "";
  const bool has_kscreen = std::system("command -v kscreen-doctor >/dev/null 2>&1") == 0;
  std::shared_ptr<DesktopBackend> b;
  if (select_backend_kind(desktop, has_kscreen) == BackendKind::KWin)
    b = std::make_shared<KWinBackend>();
  else
    b = std::make_shared<GenericBackend>();
  std::fprintf(stderr, "desktop backend: %s\n", b->name());
  return b;
}

}  // namespace droppix
```

- [ ] **Step 4: Register in CMake**

In `host/CMakeLists.txt`, add to the `droppix_core` source list (after `src/aoa_scan.cpp`):

```cmake
  src/desktop_backend.cpp
```

And add to the `droppix_tests` source list (after `tests/test_aoa_scan.cpp`):

```cmake
  tests/test_desktop_backend.cpp
```

- [ ] **Step 5: Build + run the selector tests**

Run:
```
distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . -j"$(nproc)" && ctest --output-on-failure -R DesktopBackend'
```
Expected: all 6 `DesktopBackend.*` tests PASS; full build succeeds (stream_daemon unchanged, so its statics coexist harmlessly with the new unit).

- [ ] **Step 6: Commit**

```bash
git add host/src/desktop_backend.h host/src/desktop_backend.cpp host/tests/test_desktop_backend.cpp host/CMakeLists.txt
git commit -m "feat(desktop-m1): DesktopBackend interface + KWin/Generic backends + pure selector"
```

---

### Task 2: Rewire `StreamDaemon` onto `DesktopBackend`

**Files:**
- Modify: `host/src/stream_daemon.h`, `host/src/stream_daemon.cpp`

**Interfaces:**
- Consumes: `DesktopBackend`, `make_desktop_backend()`, `user_session_prefix()` (Task 1).
- Produces: no new public interface — removes the three file-statics and routes through `desktop_`.

- [ ] **Step 1: Add the backend member to `stream_daemon.h`**

Add the include near the top of `host/src/stream_daemon.h` (with the other includes):

```cpp
#include <memory>
#include "desktop_backend.h"
```

Add a private member to the `StreamDaemon` class (next to its other members):

```cpp
  std::shared_ptr<DesktopBackend> desktop_ = make_desktop_backend();
```

- [ ] **Step 2: Remove the three statics + include from `stream_daemon.cpp`**

In `host/src/stream_daemon.cpp`:
- Delete the `user_cmd_prefix()` function (the `static std::string user_cmd_prefix() { … }` block, ~lines 18-35 including its doc comment).
- Delete the `run_kscreen()` function (~lines 37-47).
- Delete the `safe_output_name()` function (~lines 49-55).
- Delete the `bind_touch_to_output(...)` function (~lines 57-91 including its doc comment).
- Add `#include "desktop_backend.h"` with the other includes (if not already pulled in via `stream_daemon.h`).

- [ ] **Step 3: Route the three call sites through the backend**

In `run_until` (`host/src/stream_daemon.cpp`):

Replace both `parse_kscreen_outputs(run_kscreen())` calls:
```cpp
  std::vector<OutputInfo> before_outputs = desktop_->outputs();
```
```cpp
  auto after_outputs = desktop_->outputs();
```

Replace the detached touch-bind thread (currently `std::thread(bind_touch_to_output, droppix.name, cfg_.touch_name).detach();`) with one that captures a `shared_ptr` copy so the backend outlives the daemon if the ~10 s bind is still running at teardown:
```cpp
        auto backend = desktop_;                       // shared_ptr copy keeps it alive
        std::string out_name = droppix.name, tname = cfg_.touch_name;
        std::thread([backend, out_name, tname]{ backend->map_touch(out_name, tname); }).detach();
```

Replace the audio prefix call (`audio.start(user_cmd_prefix())`):
```cpp
    if (audio.start(user_session_prefix()))
```

- [ ] **Step 4: Build the streamer + run the full suite**

Run:
```
distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . -j"$(nproc)" && ctest --output-on-failure 2>&1 | tail -6'
```
Expected: builds clean; **all tests pass** (161 + the 6 new `DesktopBackend` = 167). No test asserts on the removed statics; `parse_kscreen_outputs`/selectors are still covered by `test_monitor_geometry`.

- [ ] **Step 5: Commit**

```bash
git add host/src/stream_daemon.h host/src/stream_daemon.cpp
git commit -m "feat(desktop-m1): StreamDaemon runs its output/touch ops through DesktopBackend"
```

- [ ] **Step 6: Manual KDE verification (operator, documented)**

Start a stream on KDE (evdi, `--touch`); confirm the host log prints `desktop backend: kwin`, the second monitor appears, and touch maps to it — i.e. M1 is invisible on KDE.

---

## Self-Review

**Spec coverage:**
- `DesktopBackend` interface (`outputs`/`map_touch`/`name`) → Task 1. ✓
- `KWinBackend` = relocated exact behavior → Task 1 Step 3 (verbatim move). ✓
- `GenericBackend` graceful degradation → Task 1 (empty outputs + no-op log). ✓
- Pure selector + factory, detection → Task 1 (`select_backend_kind` + `make_desktop_backend`). ✓
- `user_session_prefix` shared (kscreen, touch, **audio**) → Task 1 (moved) + Task 2 Step 3 (audio rewired). ✓
- Rewire `StreamDaemon`, remove statics, shared_ptr lifetime for the detached bind → Task 2. ✓
- Zero behavior change on KDE → KWinBackend runs identical commands; selector picks KWin on KDE. ✓
- Selector unit tests → Task 1 Step 1 (6 cases). ✓

**Placeholder scan:** none — full code in every step.

**Type consistency:** `select_backend_kind(const std::string&, bool) -> BackendKind` identical in `.h`, `.cpp`, and tests. `std::shared_ptr<DesktopBackend>` used identically in the factory, the `desktop_` member, and the detached-thread capture. `user_session_prefix()` (renamed from `user_cmd_prefix`) updated at all three former call sites (kscreen→`outputs()`, touch→`map_touch()`, audio→direct call).
