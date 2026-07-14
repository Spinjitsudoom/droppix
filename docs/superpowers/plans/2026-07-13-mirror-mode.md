# Mirror Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a connected monitor mirror (clone) the host's primary screen instead of extending it, via a per-monitor Extend/Mirror toggle in the host GUI — reusing the whole evdi capture/stream pipeline by making the compositor replicate primary→evdi.

**Architecture:** The streamer takes a `--mirror` flag; in mirror it creates the evdi output at the primary's resolution and, after adopting it, tells the compositor to replicate the primary (KDE `replicationSource`, X11 `--same-as`). The host GUI toggles this per active monitor by restarting that session with the flag. **Host-only — no protocol/client/Android change.**

**Tech Stack:** C++ (host streamer + Qt6 GUI), CMake.

## Global Constraints

- **Host-only.** No `client/`, `android/`, or protocol/HELLO change.
- **Mechanism:** compositor clone (keep capturing evdi). Mirror = evdi replicates primary; Extend = today's right-of placement.
- **Compositor commands (exact):** KDE mirror `kscreen-doctor "output.<evdi>.replicationSource.<primaryId>"`; KDE extend `kscreen-doctor "output.<evdi>.replicationSource.0"`; X11 mirror `xrandr --output <evdi> --same-as <primary>`; X11 extend `xrandr --output <evdi> --auto --right-of <primary>`; Generic → none.
- **Mirror resolution:** in mirror the evdi output is CREATED at the primary's WxH (not the client's) so the clone is 1:1 (no post-creation mode switch). Default is **Extend** (unchanged behavior).
- **Safety:** all output names pass `safe_output_name` before shell interpolation. Compositor commands run via `user_session_prefix()`.
- **Build/test env** (repo on CIFS no-exec mount): `distrobox enter droppix-dev -- bash -lc 'cmake -S host -B ~/droppix-build -DDROPPIX_BUILD_TESTS=ON && cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build <-R filter> --output-on-failure'`. The GUI test target builds under the same tree.
- Work on branch `feat/mirror-mode` (off `master`). Commit after each task.

---

### Task 1: `OutputInfo` gains `id` + `primary` (parsers)

**Files:**
- Modify: `host/src/monitor_geometry.h`, `host/src/monitor_geometry.cpp`
- Test: the existing monitor-geometry parser test (find it: `grep -rl parse_kscreen_outputs host/tests`)

**Interfaces:**
- Produces: `OutputInfo { std::string name; Rect geom; bool enabled; int id = 0; bool primary = false; }`. `parse_kscreen_outputs` fills `id` (the `Output: <id>` number) + `primary` (KDE priority 1). `parse_xrandr_outputs` fills `primary` (the `primary` token); `id` stays 0 (unused on X11).

- [ ] **Step 1: Write the failing tests** — add to the parser test file

```cpp
TEST(ParseKscreen, IdAndPrimary) {
  const char* t =
    "Output: 1 DP-3\n\tenabled\n\tpriority 1\n\tGeometry: 0,0 1920x1080\n"
    "Output: 70 HDMI-2\n\tenabled\n\tpriority 2\n\tGeometry: 1920,0 1280x1024\n";
  auto o = droppix::parse_kscreen_outputs(t);
  ASSERT_EQ(o.size(), 2u);
  EXPECT_EQ(o[0].id, 1);   EXPECT_TRUE(o[0].primary);
  EXPECT_EQ(o[1].id, 70);  EXPECT_FALSE(o[1].primary);
}
TEST(ParseXrandr, PrimaryFlag) {
  const char* t =
    "eDP-1 connected primary 1920x1080+0+0 (normal)\n"
    "HDMI-2 connected 1280x1024+1920+0 (normal)\n";
  auto o = droppix::parse_xrandr_outputs(t);
  ASSERT_EQ(o.size(), 2u);
  EXPECT_TRUE(o[0].primary);   EXPECT_FALSE(o[1].primary);
}
```

- [ ] **Step 2: Run to verify FAIL**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build -R "ParseKscreen|ParseXrandr" --output-on-failure'`
Expected: FAIL (no `id`/`primary` member).

- [ ] **Step 3: Implement.**
  - `monitor_geometry.h`: `struct OutputInfo { std::string name; Rect geom; bool enabled = false; int id = 0; bool primary = false; };`
  - `parse_kscreen_outputs`: where it does `ls >> num >> name;`, set `o.id = num;`. Add a branch in the per-line loop (like the `enabled`/`Geometry:` ones): if the line contains `"priority"`, `int pr; if (std::sscanf(line.c_str() + line.find("priority"), "priority %d", &pr) == 1) outs.back().primary = (pr == 1);`.
  - `parse_xrandr_outputs`: in the token scan `while (ls >> tok)`, before the geometry `sscanf`, add `if (tok == "primary") { o.primary = true; continue; }`.

- [ ] **Step 4: Run to verify PASS** — same as Step 2. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add host/src/monitor_geometry.h host/src/monitor_geometry.cpp host/tests/<parser_test_file>
git commit -m "feat(host): OutputInfo carries id + primary (kscreen/xrandr parsers)"
```

---

### Task 2: `LayoutMode` + pure `layout_command`

**Files:**
- Modify: `host/src/desktop_backend.h`, `host/src/desktop_backend.cpp`
- Test: the desktop-backend test (find it: `grep -rl select_backend_kind host/tests`)

**Interfaces:**
- Produces: `enum class LayoutMode { Extend, Mirror };` and
  `std::string layout_command(BackendKind kind, const std::string& evdi_output, const std::string& primary_output, int primary_id, LayoutMode mode);` — the compositor command (no `user_session_prefix`/`timeout` wrapper; the caller adds those). Empty string when unsupported/unsafe.

- [ ] **Step 1: Write the failing tests** — add to the desktop-backend test

```cpp
using droppix::BackendKind; using droppix::LayoutMode; using droppix::layout_command;
TEST(LayoutCommand, KWinMirrorReplicates) {
  auto c = layout_command(BackendKind::KWin, "DVI-I-1", "DP-3", 1, LayoutMode::Mirror);
  EXPECT_NE(c.find("replicationSource.1"), std::string::npos);
  EXPECT_NE(c.find("DVI-I-1"), std::string::npos);
}
TEST(LayoutCommand, KWinExtendClears) {
  auto c = layout_command(BackendKind::KWin, "DVI-I-1", "DP-3", 1, LayoutMode::Extend);
  EXPECT_NE(c.find("replicationSource.0"), std::string::npos);
}
TEST(LayoutCommand, X11MirrorSameAs) {
  auto c = layout_command(BackendKind::X11, "DVI-I-1", "eDP-1", 0, LayoutMode::Mirror);
  EXPECT_NE(c.find("--same-as"), std::string::npos);
  EXPECT_NE(c.find("eDP-1"), std::string::npos);
}
TEST(LayoutCommand, X11ExtendRightOf) {
  auto c = layout_command(BackendKind::X11, "DVI-I-1", "eDP-1", 0, LayoutMode::Extend);
  EXPECT_NE(c.find("--right-of"), std::string::npos);
}
TEST(LayoutCommand, GenericEmpty) {
  EXPECT_TRUE(layout_command(BackendKind::Generic, "X", "Y", 0, LayoutMode::Mirror).empty());
}
TEST(LayoutCommand, UnsafeNameRejected) {
  EXPECT_TRUE(layout_command(BackendKind::X11, "X; rm -rf /", "Y", 0, LayoutMode::Mirror).empty());
}
```

- [ ] **Step 2: Run to verify FAIL** — `ctest -R LayoutCommand`. Expected: FAIL (undefined).

- [ ] **Step 3: Implement.**
  - `desktop_backend.h`: add `enum class LayoutMode { Extend, Mirror };` (near `BackendKind`) and declare `layout_command(...)`.
  - `desktop_backend.cpp`:
    ```cpp
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
    ```

- [ ] **Step 4: Run to verify PASS** — `ctest -R LayoutCommand`. Expected: PASS (6 tests).

- [ ] **Step 5: Commit**

```bash
git add host/src/desktop_backend.h host/src/desktop_backend.cpp host/tests/<backend_test_file>
git commit -m "feat(host): LayoutMode + pure layout_command builder"
```

---

### Task 3: `apply_layout` per backend

**Files:**
- Modify: `host/src/desktop_backend.h`, `host/src/desktop_backend.cpp`

**Interfaces:**
- Consumes: `OutputInfo.id/primary` (Task 1), `layout_command` (Task 2).
- Produces: `virtual bool DesktopBackend::apply_layout(const std::string& evdi_output, LayoutMode mode)` — default returns false; overridden by KWin/X11.

- [ ] **Step 1: Read** `X11Backend::adopt_output` (the `sh -c` + `user_session_prefix()` + `std::system` pattern) and `KWinBackend::outputs()` / `X11Backend::outputs()`. `apply_layout` mirrors the run pattern and uses `outputs()` to find the primary.

- [ ] **Step 2: Implement.**
  - `desktop_backend.h`: add to `DesktopBackend`: `virtual bool apply_layout(const std::string& evdi_output, LayoutMode mode) { (void)evdi_output; (void)mode; return false; }`. Override in `KWinBackend` and `X11Backend` (declare `bool apply_layout(const std::string&, LayoutMode) override;`).
  - `desktop_backend.cpp`, a shared helper + the two overrides:
    ```cpp
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
    ```
    (`GenericBackend` keeps the base no-op returning false.)

- [ ] **Step 3: Build (shell/compositor not unit-testable — on-device in Task 7)**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j 2>&1 | tail -5'`
Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add host/src/desktop_backend.h host/src/desktop_backend.cpp
git commit -m "feat(host): DesktopBackend::apply_layout (mirror/extend)"
```

---

### Task 4: Streamer `--mirror` — cfg, evdi resolution, apply

**Files:**
- Modify: `host/src/stream_daemon.h`, `host/src/stream_daemon.cpp`, `host/src/stream_main.cpp`

**Interfaces:**
- Consumes: `apply_layout`/`LayoutMode` (Task 3), `OutputInfo.primary` (Task 1).
- Produces: `StreamConfig.mirror`; `droppix_stream --mirror`.

- [ ] **Step 1: Read** `stream_main.cpp`'s arg loop (`else if (a == "--touch") touch = true;` and where the `StreamConfig` is populated) and `stream_daemon.cpp` `run_until`: the `before_outputs` snapshot (~line 26), the `make_source_(w, h)` call that creates the evdi output (~29-41), the identify + `adopt_output` block (~98-124), and `injector.set_geometry` (~190).

- [ ] **Step 2: Implement.**
  - `stream_daemon.h`: add `bool mirror = false;` to `StreamConfig` (next to `touch`).
  - `stream_main.cpp`: add `else if (a == "--mirror") mirror = true;` in the arg loop, and set `cfg.mirror = mirror;` where the other cfg fields are assigned (declare a local `bool mirror = false;` with the other flag locals).
  - `stream_daemon.cpp` `run_until`:
    - **evdi at primary res (mirror):** right before the `make_source_(w, h)` call, if `cfg_.mirror`, find the primary in `before_outputs` (an enabled output with `.primary`, else the first enabled) and override the source dims: `if (cfg_.mirror) { for (auto& o : before_outputs) if (o.enabled && o.primary) { w = o.geom.w; h = o.geom.h; break; } }` (use whatever the local width/height variables are named at that call — read them; if none has `.primary`, fall back to the first enabled non-zero output). Log the override.
    - **apply layout:** in the identify/adopt block, AFTER the existing `adopt_output` call (and its geometry re-query), add:
      ```cpp
      serviced([this, out_name]{
        return desktop_->apply_layout(out_name, cfg_.mirror ? LayoutMode::Mirror : LayoutMode::Extend);
      });
      after_outputs = query_outputs();   // re-read geometry so set_geometry/touch follow the new layout
      ```
      (Match the `serviced(...)`/`query_outputs()` helpers already used for `adopt_output`. Re-identify `droppix` geometry from `after_outputs` if the existing code does so, so `set_geometry` uses the post-layout rect.)

- [ ] **Step 3: Build + full host suite**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build --output-on-failure 2>&1 | tail -8'`
Expected: clean build; host suite green (no regressions).

- [ ] **Step 4: Commit**

```bash
git add host/src/stream_daemon.h host/src/stream_daemon.cpp host/src/stream_main.cpp
git commit -m "feat(host): streamer --mirror (evdi at primary res + apply_layout)"
```

---

### Task 5: `args_builder` — `--mirror` flag

**Files:**
- Modify: `host/gui/args_builder.h`, `host/gui/args_builder.cpp`
- Test: the args-builder test (find it: `grep -rl build_command host/gui/tests`)

**Interfaces:**
- Produces: `build_command(const Settings& s, const std::string& stream_bin, int port, const std::string& touch_name, const std::string& usb_aoa_serial, bool mirror)` — appends `--mirror` (Evdi source only) when `mirror`.

- [ ] **Step 1: Write the failing test** — add to the args-builder test

```cpp
TEST(ArgsBuilder, MirrorFlag) {
  Settings s; s.source = Settings::Source::Evdi; s.width = 1280; s.height = 800;
  auto with = build_command(s, "/bin/streamer", 5000, "droppix-touch", "", /*mirror=*/true);
  EXPECT_NE(std::find(with.args.begin(), with.args.end(), "--mirror"), with.args.end());
  auto without = build_command(s, "/bin/streamer", 5000, "droppix-touch", "", /*mirror=*/false);
  EXPECT_EQ(std::find(without.args.begin(), without.args.end(), "--mirror"), without.args.end());
}
```
(Match the existing build_command test's fixture/namespace and how it inspects `args` — for the pkexec/Evdi path `--mirror` lands in `c.args` after the binary; adjust the assertion to search the same vector the neighbouring tests search.)

- [ ] **Step 2: Run to verify FAIL** — `ctest -R ArgsBuilder`. Expected: FAIL (build_command has no `mirror` param).

- [ ] **Step 3: Implement.** `args_builder.h`: add the trailing `bool mirror = false` param to `build_command`'s declaration. `args_builder.cpp`: in the `if (s.source == Settings::Source::Evdi) { ... }` block (next to the `--touch`/`--orientation` appends), add `if (mirror) a.push_back("--mirror");`.

- [ ] **Step 4: Run to verify PASS** — `ctest -R ArgsBuilder`. Expected: PASS. Then a full build so existing `build_command` call sites still compile (the new param defaults to `false`).

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j 2>&1 | tail -4'`
Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add host/gui/args_builder.h host/gui/args_builder.cpp host/gui/tests/<args_test_file>
git commit -m "feat(host/gui): build_command --mirror flag"
```

---

### Task 6: Host GUI — per-monitor Extend/Mirror toggle

**Files:**
- Modify: `host/gui/session_manager.h`, `host/gui/main_window.h`, `host/gui/main_window.cpp`

**Interfaces:**
- Consumes: `build_command(..., mirror)` (Task 5).

- [ ] **Step 1: Read** `main_window.cpp`: `startSession(key,label,transport,port,id,direct)` (~657) — the `build_command(s, streamBin_, port, tname, aoaSerial)` call and how the monitor row is added to `monitorsList_`; and `stopSelectedMonitor()` (~722) — how it maps the selected `monitorsList_` row to a `Session` (the item's stored key). The toggle reuses this selection + the start path.

- [ ] **Step 2: Implement.**
  - `session_manager.h`: add `bool mirror = false;` to `Session`.
  - `main_window.h`: declare `void toggleSelectedMonitorMirror();` and add a member button if the toolbar is built in the header (else it's local in the setup code).
  - `main_window.cpp`:
    - Give `startSession` a trailing `bool mirror = false` param; store it in the created `Session` (`sess.mirror = mirror` where the Session is populated) and pass it: `build_command(s, streamBin_, port, tname, aoaSerial, mirror)`. Append the mode to the monitor row label (e.g. `... + (mirror ? " — Mirror" : "")`).
    - Add a **"Toggle mirror"** `QPushButton` next to the "Stop selected" button (mirror that button's creation + `connect(...)` at ~203-210), wired to `toggleSelectedMonitorMirror`.
    - Implement `toggleSelectedMonitorMirror()`: find the selected session exactly like `stopSelectedMonitor()` does (selected `monitorsList_` row → its key → `SessionManager::find`). Capture the session's `key,label,transport,port,id,direct` and `newMirror = !session.mirror`. Stop + remove that session (reuse the stop path `stopSelectedMonitor` uses — stop the controller, remove the row, `sessions_.remove(key)`), then call `startSession(key,label,transport,port,id,direct, newMirror)` to respawn it with the new flag. (If `direct` isn't stored on the Session, thread it as needed or default to the same value stopSelectedMonitor would.)

- [ ] **Step 3: Build**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build --output-on-failure 2>&1 | tail -6'`
Expected: clean build; host suite green.

- [ ] **Step 4: Commit**

```bash
git add host/gui/session_manager.h host/gui/main_window.h host/gui/main_window.cpp
git commit -m "feat(host/gui): per-monitor Extend/Mirror toggle"
```

---

### Task 7: Verification

**Files:** none.

- [ ] **Step 1: Full host build + suite**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S host -B ~/droppix-build -DDROPPIX_BUILD_TESTS=ON && cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build --output-on-failure'`
Expected: green, incl. `ParseKscreen.IdAndPrimary`, `ParseXrandr.PrimaryFlag`, `LayoutCommand.*`, `ArgsBuilder.MirrorFlag`; no regressions.

- [ ] **Step 2: On-device (KDE + X11).** Connect a tablet (extended by default). In the host console, select the monitor and toggle **Mirror** → the tablet shows a clone of the host's primary at the primary's resolution; touch controls the primary. Toggle back to **Extend** → the tablet returns to a separate second screen right-of the primary; touch controls the extended desktop. A disconnect/reconnect preserves the chosen mode. Verify on both a KDE/Wayland session and an X11 session if available.

- [ ] **Step 3: Commit any fixes; otherwise done.**

---

## Self-review notes

- **Spec coverage:** parser id/primary (T1), LayoutMode+layout_command (T2), apply_layout (T3), streamer --mirror + evdi-at-primary-res + apply (T4), args_builder flag (T5), GUI toggle (T6), verify (T7). Every spec section maps to a task.
- **Testable seams:** parsers (T1), `layout_command` (T2), `build_command` (T5), and the `--mirror` parse are unit-tested; the shell/compositor effects (apply_layout, live layout) are on-device (T7) — consistent with how touch/adopt are handled.
- **Type/name consistency:** `LayoutMode{Extend,Mirror}`, `layout_command(kind,evdi,primary,id,mode)`, `apply_layout(evdi,mode)`, `StreamConfig.mirror`, `Session.mirror`, `build_command(...,mirror)`, `--mirror` — consistent across tasks.
- **No host regressions:** extend stays the default and its path is unchanged (adopt_output still does right-of); mirror is additive. `build_command`'s new param defaults to false so existing call sites compile.
- **Safety:** `safe_output_name` gates every interpolated name in `layout_command` and `apply_layout`.
