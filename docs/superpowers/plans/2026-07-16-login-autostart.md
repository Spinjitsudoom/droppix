# Driver-at-Login Completion Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the existing "Launch Droppix at login" so it (a) starts **hidden to the tray** (`--minimized`) instead of popping the window every login, and (b) resolves a working launch command under **Flatpak** — with the exec/`.desktop` logic extracted to a pure, unit-tested module.

**Architecture:** A pure `autostart.{h,cpp}` (`autostart_exec_command` + `autostart_desktop`) that `settings_dialog.cpp`'s existing `setLaunchAtLogin` builds the file from; a `--minimized` flag in `main.cpp` routed to `MainWindow::startMinimizedToTray()` reusing the existing tray. **Host-only. No protocol/client/Android change.**

**Tech Stack:** C++ / Qt6 (host GUI), CMake, GoogleTest.

## Global Constraints

- **Host-only.** No `client/`, `android/`, or protocol change.
- **Complete, don't rebuild.** The checkbox (`launchAtLogin_`), `~/.config/autostart/droppix.desktop` path (`autostartPath()`), minimize-on-close, tray, auto-connect, and polkit pre-auth already exist — reuse them. Do NOT auto-flip other prefs from the autostart toggle.
- **Exec resolution:** `$APPIMAGE` if set → its path; else `$FLATPAK_ID` set → `flatpak run <id>`; else `applicationFilePath()`. Quote a path containing a space; `flatpak run <id>` is left unquoted (ids have no spaces). The `.desktop` builder appends ` --minimized`.
- **`--minimized`:** start hidden to the tray only if a tray is available; otherwise show the window (never strand the user).
- **Build/test env** (repo on CIFS no-exec mount): `distrobox enter droppix-dev -- bash -lc 'cmake -S host -B ~/droppix-build -DDROPPIX_BUILD_TESTS=ON && cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build <-R filter> --output-on-failure'`. (Tray/login start is GUI/session behavior — verified on-device; the pure exec/`.desktop` builders are unit-tested.)
- Work on branch `feat/login-autostart` (off `master`). Commit after each task.

---

### Task 1: Pure `autostart` module + refactor `setLaunchAtLogin`

**Files:**
- Create: `host/gui/autostart.h`, `host/gui/autostart.cpp`
- Modify: `host/gui/settings_dialog.cpp`, `host/CMakeLists.txt`
- Test: `host/tests/test_autostart.cpp`

**Interfaces:**
- Produces: `struct AutostartEnv { std::string appimage, flatpak_id, app_path; };` `std::string autostart_exec_command(const AutostartEnv&);` `std::string autostart_desktop(const std::string& exec_cmd);` (namespace `droppix`).

- [ ] **Step 1: Write the failing tests** — `host/tests/test_autostart.cpp`

```cpp
#include <gtest/gtest.h>
#include "autostart.h"
using namespace droppix;
TEST(Autostart, ExecPrefersAppImage) {
  EXPECT_EQ(autostart_exec_command({"/x/Droppix.AppImage", "org.droppix.Droppix", "/sandbox/bin"}),
            "/x/Droppix.AppImage");
}
TEST(Autostart, ExecFlatpakWhenNoAppImage) {
  EXPECT_EQ(autostart_exec_command({"", "org.droppix.Droppix", "/app/bin/droppix_gui"}),
            "flatpak run org.droppix.Droppix");
}
TEST(Autostart, ExecAppPathFallbackQuotesSpaces) {
  EXPECT_EQ(autostart_exec_command({"", "", "/usr/bin/droppix_gui"}), "/usr/bin/droppix_gui");
  EXPECT_EQ(autostart_exec_command({"", "", "/home/u/My Apps/droppix_gui"}),
            "\"/home/u/My Apps/droppix_gui\"");
}
TEST(Autostart, DesktopHasMinimizedAndAutostartFlag) {
  auto d = autostart_desktop("/usr/bin/droppix_gui");
  EXPECT_NE(d.find("Exec=/usr/bin/droppix_gui --minimized\n"), std::string::npos);
  EXPECT_NE(d.find("X-GNOME-Autostart-enabled=true"), std::string::npos);
  EXPECT_NE(d.find("Type=Application"), std::string::npos);
}
```

- [ ] **Step 2: Run to verify FAIL**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B ~/droppix-build -DDROPPIX_BUILD_TESTS=ON && cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build -R Autostart --output-on-failure'`
Expected: FAIL (no `autostart.h`).

- [ ] **Step 3: Implement.**
  - `autostart.h`: the struct + two declarations (namespace `droppix`, `#include <string>`).
  - `autostart.cpp`:
    ```cpp
    #include "autostart.h"
    namespace droppix {
    std::string autostart_exec_command(const AutostartEnv& e) {
      auto quote = [](const std::string& p) {
        return p.find(' ') != std::string::npos ? "\"" + p + "\"" : p;
      };
      if (!e.appimage.empty())   return quote(e.appimage);
      if (!e.flatpak_id.empty()) return "flatpak run " + e.flatpak_id;
      return quote(e.app_path);
    }
    std::string autostart_desktop(const std::string& exec_cmd) {
      return "[Desktop Entry]\n"
             "Type=Application\n"
             "Name=Droppix\n"
             "Comment=Use a tablet as a second monitor\n"
             "Exec=" + exec_cmd + " --minimized\n"
             "Icon=droppix\n"
             "Terminal=false\n"
             "X-GNOME-Autostart-enabled=true\n";
    }
    }  // namespace droppix
    ```
  - `settings_dialog.cpp`: refactor the anonymous-namespace `setLaunchAtLogin` to build the file via the pure functions (`#include "autostart.h"`):
    ```cpp
    void setLaunchAtLogin(bool on) {
      const QString path = autostartPath();
      if (!on) { QFile::remove(path); return; }
      QDir().mkpath(QFileInfo(path).absolutePath());
      const AutostartEnv env{ qEnvironmentVariable("APPIMAGE").toStdString(),
                              qEnvironmentVariable("FLATPAK_ID").toStdString(),
                              QCoreApplication::applicationFilePath().toStdString() };
      const std::string content = autostart_desktop(autostart_exec_command(env));
      QFile f(path);
      if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(content.c_str(), static_cast<qint64>(content.size()));
    }
    ```
    (Remove the old inline `$APPIMAGE`/`QTextStream` body — the pure module now owns the exec resolution + `.desktop` text, and it now emits `--minimized` + the Flatpak case.)
  - `host/CMakeLists.txt`: add `gui/autostart.cpp` to the `droppix_gui` sources (near `gui/settings_dialog.cpp`), and add BOTH `tests/test_autostart.cpp` and `gui/autostart.cpp` to the `droppix_tests` target (the same way `gui/args_builder.cpp` + `tests/test_args_builder.cpp` are wired — `droppix_tests` already `target_include_directories(... PRIVATE gui)`).

- [ ] **Step 4: Run to verify PASS**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B ~/droppix-build -DDROPPIX_BUILD_TESTS=ON && cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build -R Autostart --output-on-failure'`
Expected: PASS (4 Autostart tests). (If Qt6 is available the `droppix_gui` also builds, proving the `settings_dialog.cpp` refactor compiles.)

- [ ] **Step 5: Commit**

```bash
git add host/gui/autostart.h host/gui/autostart.cpp host/gui/settings_dialog.cpp host/CMakeLists.txt host/tests/test_autostart.cpp
git commit -m "feat(host/gui): pure autostart module (Flatpak exec + --minimized); refactor setLaunchAtLogin"
```

---

### Task 2: `--minimized` → start hidden to tray

**Files:**
- Modify: `host/gui/main.cpp`, `host/gui/main_window.h`, `host/gui/main_window.cpp`

**Interfaces:**
- Produces: `void MainWindow::startMinimizedToTray();`

- [ ] **Step 1: Read** `main.cpp` (currently `MainWindow w; w.show();`), `MainWindow::setupTray()` (creates `tray_`, returns early if `!QSystemTrayIcon::isSystemTrayAvailable()`), the `tray_` member, and the tray `activated` handler that calls `showNormal()/raise()/activateWindow()` (so a later tray click restores the window).

- [ ] **Step 2: Implement.**
  - `main_window.h`: declare `public: void startMinimizedToTray();`.
  - `main_window.cpp`:
    ```cpp
    void MainWindow::startMinimizedToTray() {
      if (tray_ && QSystemTrayIcon::isSystemTrayAvailable()) {
        tray_->show();          // make the tray icon visible; keep the window hidden
      } else {
        show();                 // no tray -> don't strand the user
      }
    }
    ```
    (`tray_` is created in `setupTray()` during construction; it's null when no tray exists, so the `&& isSystemTrayAvailable()` guard falls back to `show()`. The existing tray `activated`/menu handlers already restore the window on click.)
  - `main.cpp`: parse `--minimized` and route:
    ```cpp
    bool minimized = false;
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--minimized") minimized = true;
    droppix::MainWindow w;
    if (minimized) w.startMinimizedToTray(); else w.show();
    ```
    (add `#include <string>`.)

- [ ] **Step 3: Build (tray/session behavior verified on-device in Task 3)**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j 2>&1 | tail -5'`
Expected: clean build (incl. `droppix_gui` if Qt6 is present).

- [ ] **Step 4: Commit**

```bash
git add host/gui/main.cpp host/gui/main_window.h host/gui/main_window.cpp
git commit -m "feat(host/gui): --minimized starts hidden to the tray"
```

---

### Task 3: Verification

**Files:** none.

- [ ] **Step 1: Full host build + suite**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S host -B ~/droppix-build -DDROPPIX_BUILD_TESTS=ON && cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build --output-on-failure'`
Expected: green, incl. `Autostart.*`; no regressions; `droppix_gui` builds.

- [ ] **Step 2: On-device (AppImage / plain binary — the primary path).** In the host GUI Settings, enable **Launch Droppix at login** → `~/.config/autostart/droppix.desktop` contains `Exec=… --minimized`. Log out/in → droppix starts **hidden in the tray** (no window pops up); with auto-connect on, a known tablet connects with no clicks; click the tray icon → the window restores. Disable the checkbox → the file is removed and it no longer autostarts. Run `droppix_gui --minimized` on a session with no tray → the window shows (fallback).
  - **Flatpak note (known limitation, not fixed here):** the exec string is now correct (`flatpak run org.droppix.Droppix --minimized`), but under Flatpak the `.desktop` is written to the sandboxed `~/.var/app/<id>/config/autostart/` (via `QStandardPaths`), which the host session doesn't scan — so it does **not** actually autostart. Making it fire needs a host-side write (Background portal / `flatpak-spawn --host`) — a backlog follow-up. Do NOT claim Flatpak autostart works.

- [ ] **Step 3: Commit any fixes; otherwise done.**

---

## Self-review notes

- **Spec coverage:** pure exec/`.desktop` builder + Flatpak + refactor (T1); `--minimized` start-to-tray (T2); verify (T3). Every corrected-spec gap maps to a task.
- **Completes, doesn't rebuild:** reuses `autostartPath()`, `launchAtLogin_`, `setupTray`/`tray_`, minimize-on-close, auto-connect, polkit — only the exec resolution (Flatpak), the `--minimized` emission, and the start-to-tray path are new.
- **Testable seam:** `autostart_exec_command`/`autostart_desktop` are pure (no Qt) → unit-tested in `droppix_tests` like `args_builder`; the tray/login behavior is on-device.
- **Type consistency:** `AutostartEnv`/`autostart_exec_command`/`autostart_desktop`/`startMinimizedToTray` consistent across tasks.
- **No strand:** `--minimized` falls back to `show()` when no tray; disabling removes the file.
