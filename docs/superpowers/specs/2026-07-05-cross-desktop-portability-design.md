# Cross-Desktop Portability — Scoping Roadmap

**Date:** 2026-07-05
**Type:** Multi-milestone roadmap (decomposition), not a single-feature spec.
**Status:** Partial on master (2026-07-18): M1 DesktopBackend + KWin + X11 backends shipped; M2 Sway / M3 GNOME Wayland still open. (Note: original text deferred X11; an X11Backend was added later.)

## Goal

Make droppix's extended-monitor path work on Wayland desktops beyond KDE Plasma —
specifically **Sway/wlroots** and **GNOME/Mutter** — by decoupling the streamer
from KWin-specific tooling.

**Update (2026-07-18):** M1 (`DesktopBackend` + `KWinBackend` + `GenericBackend`)
and an **`X11Backend`** (originally deferred in this roadmap) are on `master`.
Remaining open work is **M2 Sway** and **M3 GNOME Wayland**. The historical
sections below still describe the original Wayland-first plan; trust the Status
line and [`docs/STATUS.md`](../../STATUS.md) for what has shipped.

## Key finding: the KWin coupling is narrow

Auditing the host code, only two operations are KWin/KDE-specific, both in
`host/src/stream_daemon.cpp`:

| Concern | How it's done today | Portability |
|---|---|---|
| **Composite the evdi output** (second screen appears) | No droppix API call — the streamer creates the evdi DRM device and waits for the compositor to set a mode on it (`evdi_frame_source.cpp`, `spike_main.cpp`) | Compositor's own job. KWin adopts it; GNOME/wlroots Wayland are expected to (DisplayLink/evdi support); **X11 does not.** |
| **Enumerate outputs** (name/enabled/geometry) | `kscreen-doctor -o` (`run_kscreen()`) | KDE-only; touch/geometry path only |
| **Map touch → the output** | KWin DBus `org.kde.KWin.InputDevice` (`bind_touch_to_output()`) | KDE-only; touch path only |
| Position/rotate the output | Nothing — droppix never sets these | N/A |
| Audio | PipeWire `pw-record` + `pactl` | Already portable (any PipeWire system) |

So the **display itself is portable for free** to any compositor that adopts an
evdi output; only the touch/geometry path is KWin-locked. This is the whole
reason the effort is "small abstraction + per-desktop backends" rather than a
display-layer rewrite.

## Architecture: the `DesktopBackend` seam

Extract the two coupled operations behind one interface (types `OutputInfo` /
`Rect` already exist in `host/src/monitor_geometry.h` and are pure — reused):

```cpp
struct DesktopBackend {
  virtual ~DesktopBackend() = default;
  virtual std::vector<OutputInfo> outputs() = 0;                 // name/enabled/geometry
  virtual void map_touch(const std::string& output,
                         const std::string& touch_dev) = 0;      // best-effort
};
DesktopBackend* make_desktop_backend();   // runtime detection: KDE / Sway / GNOME / (X11)
```

- **`KWinBackend`** reproduces today's exact behavior (`kscreen-doctor -o` +
  KWin DBus), just relocated behind the interface. No behavior change on KDE.
- **Detection** keys off `XDG_CURRENT_DESKTOP` / `XDG_SESSION_TYPE` plus which
  session tools are present (`kscreen-doctor`, `swaymsg`, GNOME).
- **Not in the interface:** compositing the evdi output. That is the
  compositor's responsibility; no droppix code drives it. The interface only
  covers the two touch/geometry calls.
- **Shared plumbing:** the streamer runs as root and reaches the user session
  via `runuser` + a reconstructed env (`user_cmd_prefix()`). Two things need
  generalizing there, once, in a shared base: the hardcoded
  `WAYLAND_DISPLAY=wayland-0` (discover the real socket) and adding `SWAYSOCK`
  for Sway.

## Milestones

Each is an independent spec → plan → build cycle. **M0 gates M2/M3.**

| # | Milestone | Effort | Deliverable |
|---|---|---|---|
| **M0** | **Validation spike.** Run droppix unmodified under Sway, GNOME Wayland, and Cinnamon's experimental Wayland session; record whether the evdi second screen appears. | XS (hours, no feature code) | Findings note: which compositors adopt evdi for free. Go/no-go for M2/M3. |
| **M1** | `DesktopBackend` interface + `KWinBackend` extraction + runtime detection. | S–M | No behavior change on KDE; the seam exists and detection + parsing are unit-tested. |
| **M2** | Sway/wlroots backend: `outputs()` via `swaymsg -t get_outputs` (JSON); `map_touch()` via `swaymsg input <id> map_to_output <output>`; `WAYLAND_DISPLAY`/`SWAYSOCK` discovery. | M | Full droppix (display + touch) on Sway. |
| **M3** | GNOME/Mutter backend: `outputs()` via `org.gnome.Mutter.DisplayConfig` (or `gnome-monitor-config list`); `map_touch()` via `gsettings`/`org.gnome.desktop.peripherals` touchscreen output association. | M–L | Full droppix on GNOME Wayland. |

Sway before GNOME: wlroots exposes clean JSON and a documented
`map_to_output`; Mutter's `DisplayConfig` is a heavy nested variant and GNOME's
per-device touch mapping is fiddlier.

## Risks

- **M0 is a real go/no-go gate, not a formality.** The "display is free on
  non-KWin Wayland" claim is an assumption until measured. If a compositor
  doesn't adopt an evdi output unmodified (e.g. needs a specific evdi build or a
  DisplayLink userspace daemon), that milestone grows or drops. Commit to
  M2/M3 only after M0 reports green.
- **Root → user-session env** must be validated per compositor: `SWAYSOCK` for
  Sway, the real `WAYLAND_DISPLAY` and session bus for GNOME. `KWinBackend`
  already proves the pattern works.
- **Mutter `DisplayConfig` parsing** is the main cost of M3 (large nested
  D-Bus variant).
- **No cross-compositor touch-to-output standard** — each backend maps touch
  its own way. Accepted; that is exactly what the interface hides.

## Success criteria

On a supported Wayland session (KDE, Sway, GNOME), a user gets a working
extended monitor **and** touch mapped to it, with the correct backend
auto-selected and no KDE-specific tools required outside `KWinBackend`. X11
remains explicitly unsupported for display.

## Out of scope (YAGNI)

- **The X11 display path** — Xorg not adopting evdi is a separate, large effort
  (DisplayLink-style multi-GPU/PRIME). The `DesktopBackend` interface would fit
  an X11 backend for enumeration (`xrandr`) + touch (`xinput map-to-output`)
  later, but its display problem is parked here.
- **Per-compositor output positioning/rotation** — droppix already doesn't set
  these.
- **GUI changes** — backend selection is automatic; no user-facing setting.
