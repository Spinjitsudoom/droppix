# L-2026-11-25-cosmic-quirks: three COSMIC behaviours the backend has to absorb

- **ID:** `L-2026-11-25-cosmic-quirks`
- **Tags:** `host`, `desktop-backend`, `cosmic`, `gotcha`, `medium`, `lesson`
- **Date:** 2026-11-25

Found in an Arch VM with COSMIC 1.11 (cosmic-comp adopts the evdi output with no setup).

1. **An absolute pointer maps onto ONE output, not the desktop.** The right-click pointer,
   scaled across the 2560-wide desktop, landed at exactly half the intended x (1600 became 800 on
   a 1280-wide output). Fix: `DesktopBackend::pointer_maps_per_output()` (true for COSMIC). The
   streamer then scales that pointer to the droppix output alone and binds it with `map_pointer()`.
   The pointer device is now named per session (`<touch name>-rc`) so tablets don't share one
   mapping.
2. **Arrangements persist** (`~/.local/state/cosmic-comp/outputs.ron`). After one mirror session
   the droppix monitor came back mirroring every time, and a session that disabled the main
   screen left it disabled. Fix: extend mode runs `cosmic-randr enable <output>` when the output
   is restored as a mirror.
3. **A mirroring output isn't in the layout**, so touch mapped to it is dropped. Fix:
   `cosmic_touch_target()` maps touch/pen to the mirror source instead.

Touch and pen mapping use cosmic-comp's per-device `map_to_output` in the user's RON file
`~/.config/cosmic/com.system76.CosmicComp/v1/input_devices`. Only droppix's own entries are
replaced; the write is under a lock and atomic, owned by the user. COSMIC also ignores GTK's
`fullscreen_on_monitor`. To test a window on the droppix output, disable the main output, open
the window, then re-enable it.
