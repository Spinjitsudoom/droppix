# L-2026-11-25-plasma5-three-breaks: Plasma 5.27 never got touch (three separate causes)

- **ID:** `L-2026-11-25-plasma5-three-breaks`
- **Tags:** `host`, `kwin`, `desktop-backend`, `silent-failure`, `high`, `lesson`
- **Date:** 2026-11-25

## Symptom

On Plasma 5.27 (Ubuntu 24.04, the same stack as Kubuntu 24.04 LTS and Debian 12), the KWin
backend was picked, the picture came up, and then: `input: kscreen query returned 0 bytes`,
`could not identify the droppix output`, `touch disabled`.

## Root causes (each hid the next)

1. **Qt platform.** The root streamer runs `kscreen-doctor` through `runuser` with
   `WAYLAND_DISPLAY` but no `XDG_SESSION_TYPE`. Qt 5 then defaults to xcb, can't open XWayland
   (no Xauthority), and prints nothing. Qt 6 prefers Wayland on its own, which is why Plasma 6
   never showed it.
2. **Output format.** Plasma 5's `kscreen-doctor -o` prints each output on one line (state,
   priority and geometry included). `parse_kscreen_outputs` took only the name from the
   `Output:` line and expected the rest on later lines, the Plasma 6 layout.
3. **DBus API.** The touch binder enumerated `InputDeviceManager.ListTouch`, which KWin 5.27
   doesn't have. (`mapToWorkspace` doesn't exist either; setting it just logs an error.)

## Fix

1. `wayland_session_env()` adds `XDG_SESSION_TYPE=wayland QT_QPA_PLATFORM='wayland;xcb'` to the
   session prefix whenever a live `wayland-N` socket exists.
2. The `Output:` line now also yields the state word after the name, and falls through to the
   geometry/priority parsing (test `KscreenParse.Plasma5SingleLineOutputs`).
3. The touch binder enumerates `devicesSysNames`, which both Plasma 5 and 6 have, as the pen
   binder already did.

VM-verified on Plasma 5.27: taps at (640,400)/(320,200), right-click at (320,200), pen at
(960,600), all on the droppix screen.
