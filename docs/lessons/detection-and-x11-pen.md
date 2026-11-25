# L-2026-11-25-detection-and-x11-pen: stale Wayland locks, installed-but-idle KDE, and X11's lazy pen devices

- **ID:** `L-2026-11-25-detection-and-x11-pen`
- **Tags:** `host`, `desktop-backend`, `x11`, `pen`, `wrong-answer`, `medium`, `lesson`
- **Date:** 2026-11-25

Found re-testing an Xfce X11 session in a VM that had also run GNOME, Sway and Plasma.

1. **A compositor leaves `wayland-N` and `wayland-N.lock` behind** when it exits. "The lock file
   exists" therefore meant nothing, and the X11 session was taken for Wayland. Fix:
   `live_runtime_entries()` drops any pair whose lock isn't currently `flock`-held (libwayland and
   Smithay hold it while running). Everything that picks the socket or decides
   Wayland-vs-X11 uses it.
2. **`kscreen-doctor` installed ≠ KDE running.** With the desktop name gone under pkexec, an Xfce
   session on a machine that also had Plasma installed got the KWin backend. Fix: the
   unknown-desktop KDE fallback now also requires `org.kde.KWin` on the session bus. The bus
   probes use `dbus-send`, which every D-Bus system has; `gdbus` is GLib's.
3. **X11 pen:** X's libinput driver creates a tablet's pointing devices (`<pen> Pen (0)`,
   `<pen> Eraser (0)`) only on the tool's first proximity. Until then the parent device sits on
   the keyboard side, and `xinput map-to-output` on it fails with `BadMatch`. Fix: on X11 the
   injector sends one proximity in/out for pen and eraser 0.5 s after creating the device
   (`announce_pen_tools_in`), and the X11 binder maps every `<pen> …` child device.

Verified on Xfce/X11 with `xdotool getmouselocation`: right-click, taps and the pen land at the
intended points on the droppix screen.
