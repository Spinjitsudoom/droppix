# Driver at login — complete the autostart (unobtrusive start + Flatpak)

**Date:** 2026-07-16
**Status:** Shipped on master (2026-07-16) — AppImage/plain-binary login autostart is `--minimized` (starts hidden to tray). **Flatpak autostart *firing* is a documented follow-up (see below).** Scope corrected after reading the code: "Launch at login" already existed — this completed it.
**Roadmap:** tier T3 "Driver service at boot" (HOST) — the achievable "available after login" increment.

## What already exists (do not rebuild)

The host GUI **already** has this feature partly built:
- A **"Launch Droppix at login"** checkbox in `SettingsDialog` (`launchAtLogin_`), seeded from file existence, that calls `setLaunchAtLogin(bool)` — which writes/removes `~/.config/autostart/droppix.desktop`.
- A **"Minimize to tray on close"** checkbox (`minimizeOnClose_`) + the `minimize_on_close` marker + `MainWindow::minimizeToTrayRequested()` (works today).
- The **tray** (`setupTray`, `QSystemTrayIcon`), **auto-connect** (`evaluateAutoConnect`), and the **polkit pre-auth** (`/etc/polkit-1/rules.d/49-droppix.rules`, so `pkexec` streamers never prompt) — all already shipped.

## The two real gaps this fills

1. **The autostart start is intrusive.** `setLaunchAtLogin` writes `Exec=<binary>` with **no `--minimized`**, and `main.cpp` always calls `w.show()`. So every login pops the window up — the opposite of "available in the background." Fix: `Exec=<launch> --minimized` + a real `--minimized` start-hidden-to-tray path.
2. **Flatpak autostart exec string was wrong.** The `Exec` resolved to `applicationFilePath()` — an **in-sandbox** path the host session can't launch. Fix: detect `$FLATPAK_ID` → `flatpak run <id> --minimized` (the correct host-launchable command).
   > **Follow-up (not delivered here):** under Flatpak the `.desktop` is written via `QStandardPaths`, which the sandbox remaps to `~/.var/app/<id>/config/autostart/` — a path the host session does **not** scan, so autostart still doesn't *fire* under Flatpak. Making it fire needs a host-side write (the freedesktop **Background portal** `RequestBackground(autostart=true)`, or a `flatpak-spawn --host` write to the real `~/.config/autostart/`, plus a host-aware exists-check for the checkbox). This is a documented backlog item — **AppImage and plain-binary autostart (the primary distribution) work fully**; the Flatpak firing was already broken before this change (this at least prepares the correct exec string).

Plus: the exec/`.desktop` logic is inline free functions in `settings_dialog.cpp` (untestable) — **extract to a pure, unit-tested `autostart.{h,cpp}`**.

## Why this scope (not a pre-login system daemon)

The host drives the user's KDE session (kscreen/KWin, PipeWire, adb) — it must run **inside the user session**, so this is a login autostart, not a pre-login system service. The roadmap's "hard part" (unattended root streamer) is already solved by the shipped polkit rule. A true pre-login root daemon (evdi owned by a system service, GUI over a socket) is a separate architectural item, explicitly out of scope.

## Decisions

| Question | Decision |
| --- | --- |
| Toggle / mechanism | **Keep** the existing "Launch Droppix at login" checkbox + `~/.config/autostart/droppix.desktop` (unchanged UX). |
| Launch command | `autostart_exec_command(appimage, flatpak_id, app_path)` → `$APPIMAGE` if set; else `flatpak run <flatpak_id>` if `$FLATPAK_ID` set; else `app_path` (quoted if it has spaces). **Adds the Flatpak case.** |
| Unobtrusive start | `.desktop` `Exec` gets a trailing **`--minimized`**; `droppix_gui --minimized` starts **hidden to the tray** (no window). No tray available → `show()` (never strand the user). |
| Prerequisites | **Keep the existing independent toggles** — do NOT auto-flip auto-connect / remember-auth / minimize-on-close from the autostart checkbox (they're separate, already-shipped controls; silently changing them is worse UX). The Settings dialog groups them together so the user sees all three. |
| Scope | Host-only; freedesktop autostart; complete-not-rebuild. No pre-login system daemon. |

## Pure module (`host/gui/autostart.{h,cpp}` — unit-tested)

- `struct AutostartEnv { std::string appimage, flatpak_id, app_path; };`
- `std::string autostart_exec_command(const AutostartEnv&)` — the resolution above (no `--minimized`; that's the builder's job).
- `std::string autostart_desktop(const std::string& exec_cmd)` — the `.desktop` contents with `Exec=<exec_cmd> --minimized` and the existing keys (`Type/Name/Comment/Icon/Terminal/X-GNOME-Autostart-enabled=true`).
- `settings_dialog.cpp`'s `setLaunchAtLogin` is refactored to build the file via these (reading the env with `qEnvironmentVariable`), so the write path is a thin wrapper over the tested pure functions.

## `--minimized` (`host/gui/main.cpp`, `main_window`)

- `main.cpp`: if `argv` contains `--minimized`, call `w.startMinimizedToTray()` instead of `w.show()`.
- `MainWindow::startMinimizedToTray()`: if `QSystemTrayIcon::isSystemTrayAvailable()`, ensure the tray icon is visible and keep the window hidden; else `show()`. Reuses the existing `tray_`/`setupTray` machinery. (When the user later clicks the tray, the existing activate handler shows the window.)

## Testing

- **Unit (pure, no GUI):** `autostart_exec_command` — returns the AppImage path when `appimage` set; `flatpak run com.x.Y` when `flatpak_id` set (and appimage empty); the app path otherwise; a spaced app path is quoted. `autostart_desktop(exec)` — contains `Exec=<exec> --minimized` and `X-GNOME-Autostart-enabled=true`.
- **On-device:** enable "Launch at login" → `~/.config/autostart/droppix.desktop` has `Exec=… --minimized`; log out/in → droppix starts **hidden in the tray** (no window), and a known tablet auto-connects (with auto-connect on); click the tray → window shows; disable → file removed. `--minimized` with no tray shows the window.

## Out of scope

- A pre-login **system** service / root daemon owning evdi (separate architectural item).
- Auto-flipping the other prefs from the autostart toggle (kept independent).
- Non-freedesktop autostart (Windows/macOS); tray quirks beyond `QSystemTrayIcon`.
- No protocol/client/Android change.
