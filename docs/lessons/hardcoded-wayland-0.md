# L-2026-11-24-hardcoded-wayland-0: touch silently disabled because Plasma was on wayland-1

- **ID:** `L-2026-11-24-hardcoded-wayland-0`
- **Tags:** `host`, `desktop-backend`, `input`, `silent-failure`, `high`
- **Date:** 2026-11-24

## Symptom

The monitor streamed fine but touch did nothing. No `droppix-touch-*` device existed in
`/proc/bus/input/devices`. The streamer log said `input: kscreen query returned 0 bytes`.

## Root cause

The root streamer runs user-session tools (`kscreen-doctor`, KWin DBus) through
`user_session_prefix()`, which rebuilt the session env with a hardcoded
`WAYLAND_DISPLAY=wayland-0`. This Plasma session ran on `wayland-1`, so `kscreen-doctor`
printed nothing. The droppix output was never identified, and `StreamDaemon` deliberately
skips touch when it can't bind the touchscreen to the right output ("better no touch than
wrong-screen touch"). The same failure breaks orientation and placement, which also need
the output.

Reproduce as root: the same command printed 0 bytes with `wayland-0` and 5846 with `wayland-1`.

## Fix

`pick_wayland_display()` reads `$XDG_RUNTIME_DIR` and picks the lowest `wayland-N` that
also has `wayland-N.lock` (a dead compositor leaves a socket but no lock), falling back to
`wayland-0`. Unit-tested in `test_desktop_backend.cpp`.
