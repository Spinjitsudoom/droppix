# GNOME Wayland backend (M3)

**Date:** 2026-11-25
**Status:** Shipped on master (2026-11-25). Approved by delegation: the user went to bed and said "feel free to make the necessary decisions".
**Update (build):** VM testing changed three things. (1) Mirror needed no EDID change: the streamer already sizes the evdi mode to the primary in mirror mode, so a shared resolution always exists. (2) A reconnect needs a revive step, because Mutter keeps the old monitor "on" and never modes the new connection (lesson `gnome-reconnect-no-mode`). (3) The stylus fix turned out to be cross-desktop (lesson `libinput-rejects-pen`). Absolute right-click mapping works on Mutter unchanged, including at 200 %.
**Roadmap:** M3 of [`2026-07-05-cross-desktop-portability-design.md`](2026-07-05-cross-desktop-portability-design.md). First of four desktop projects (GNOME → Sway/Hyprland → Plasma 5 → COSMIC).

## Intent

The user wants droppix to have full support on every mainstream desktop Linux distro. GNOME on Wayland is the biggest gap: it is the default desktop on Ubuntu, Fedora Workstation, Debian, Zorin, Bluefin and Bazzite GNOME. ChromeOS, WSL, SteamOS and Raspberry Pi are explicitly out of scope ("my targets are desktop OSes").

## What happens on GNOME today

`make_desktop_backend()` picks `GenericBackend` (no "kde"/"plasma" in the desktop string, not X11). Its `outputs()` returns `{}`, so `StreamDaemon` cannot identify the droppix output. The result is that touch, pen and right-click are **disabled outright** ("input: touch disabled — could not identify/bind the droppix output"). Mirror, placement and the monitor grid do nothing. The picture itself may still appear, because Mutter adopts evdi outputs itself.

## Goals

On GNOME Wayland (GNOME 42+, i.e. Ubuntu 22.04 onward), with the correct backend auto-selected:

1. The droppix output is identified reliably, including when GNOME applies fractional/HiDPI scaling to it.
2. Touch lands on the droppix screen.
3. The stylus lands on the droppix screen.
4. The two-finger right-click opens the menu at the tap point.
5. Extend places the screen to the right of the primary, and the client monitor-grid (`wall_col`/`wall_row`) places it where asked.
6. Mirror works (see Mirror below for what that means on GNOME).
7. Multiple tablets at once each get their own touch/pen mapping.

## Non-goals / anti-goals

- **No change in behavior on KDE or X11.** Their test suites must stay green, and KDE gets re-checked on the Nexus.
- **The user's saved GNOME display settings are not overwritten.** droppix's layout changes are applied as *temporary* (method 1), never persistent, so `~/.config/monitors.xml` is untouched.
- **No GNOME Shell extension, and no new library dependency for the streamer.** It stays free of Qt and GLib at link time.
- **No GUI changes.** Backend selection is automatic.

## Approaches considered

| | Approach | Trade-off |
|---|---|---|
| **A (chosen)** | Mutter's D-Bus interface `org.gnome.Mutter.DisplayConfig` (`GetCurrentState` / `ApplyMonitorsConfig`), called through `gdbus` | Works on every GNOME in use (42+). Cost: parsing GVariant text output. |
| B | `gdctl` CLI | Clean, but GNOME 48+ only, so it misses Ubuntu 24.04 LTS and Debian 13. |
| C | `gdctl` when present, else A | Two code paths for no user-visible gain. |

The user approved A. Touch and pen mapping is the same under all three: GNOME's per-device gsettings.

## Design

### Shape

- `GnomeBackend : DesktopBackend` in `desktop_backend.{h,cpp}`.
- `BackendKind::Gnome`.
- `layout_command` / `place_command` keep returning `{}` for Gnome. GNOME layout is one full `ApplyMonitorsConfig` call, not a per-output command. The Gnome overrides build that call themselves from pure helpers.

All session calls go through the existing `user_session_prefix()` (runuser + `DBUS_SESSION_BUS_ADDRESS`), using `gdbus call --session`. gdbus ships with GLib, so it is present on every GNOME install, and it doesn't depend on systemd's `busctl`.

### Detection

`select_backend_kind` gains a `has_mutter` input. Priority:

1. KDE.
2. Otherwise **Gnome**, if `XDG_CURRENT_DESKTOP` contains "gnome" (case-insensitive) *or* the session bus has an owner for `org.gnome.Mutter.DisplayConfig`.
3. Otherwise X11.
4. Otherwise Generic.

The D-Bus probe is the reliable signal: under pkexec, `XDG_CURRENT_DESKTOP` is not passed to the root streamer. It is also how GNOME-derived sessions are caught, such as Ubuntu's `ubuntu:GNOME` and Zorin.

A GNOME session on **X11** keeps the X11 backend. Its xrandr/xinput path is already proven, so the Mutter check only wins on Wayland.

### Reading outputs: `GetCurrentState`

A pure parser, `parse_mutter_state(text) -> std::vector<MutterMonitor>`, unit-tested against captured real output from both test VMs. It reads the GVariant text form of `GetCurrentState`'s reply:

- per physical monitor: connector, vendor, product, serial, current mode (id, width, height);
- per logical monitor: x, y, scale, transform, primary, and its connectors.

From that, `outputs()` builds `OutputInfo`s. Geometry is in **layout (logical) pixels**: on Wayland, GNOME uses the logical layout mode, so a 2560×1600 tablet at 200 % occupies a 1280×800 rectangle. Touch scaling and the right-click pointer need that rectangle.

### Identifying the droppix output

The current `select_droppix` matches by **size == mode**. That breaks under GNOME scaling: the tablet's logical size is not its mode. On GNOME the droppix output is identified as the monitor whose **EDID serial matches this session's EDID serial**. droppix already writes the session port into the EDID serial so each session's output is unique.

If no serial matches (for example, a format surprise), fall back to the existing rule: the output newly enabled since the source started, matched by **mode size** rather than logical size.

### Touch and stylus mapping

GNOME maps an input device to a monitor through relocatable gsettings, keyed by the device's USB vendor:product id:

- touch: schema `org.gnome.desktop.peripherals.touchscreen`, path `/org/gnome/desktop/peripherals/touchscreens/<vid>:<pid>/`
- pen: schema `org.gnome.desktop.peripherals.tablet`, path `/org/gnome/desktop/peripherals/tablets/<vid>:<pid>/`

In both, the key is `output = ['<vendor>', '<product>', '<serial>']`. That is the monitor's EDID identity **exactly as `GetCurrentState` reports it**, so the strings are copied, never predicted. The value is written with `gsettings set` through the session prefix. Mutter applies it live.

**Multi-tablet collision.** Every session's touch device is currently `1209:d701` and every pen `1209:d704`, so on GNOME two tablets would share one setting and both map to the same screen. Each session's touch and pen devices therefore get a **per-session product id**. The scheme is the builder's call: stable per session and inside the 0x1209 range already used.

On KWin and X11, which match by device *name*, the id change is invisible.

**Cleanup.** The setting is keyed by product id and droppix recreates ids per session, so a stale mapping only ever points at a droppix monitor that no longer exists. That is harmless, and no cleanup is needed. *(Builder may revisit this.)*

### Right-click pointer

The right-click pointer is an absolute uinput device scaled across `desktop_bounds()`, which is the logical bounding box of enabled outputs. **This is a risk to verify first:** whether Mutter maps absolute pointer motion across the whole logical desktop the way KWin does. If it doesn't, the fallback is to map this device to the droppix output too and scale it to that output alone (the same gsettings route if Mutter treats it as a tablet, otherwise adjusting the injector's scaling). Decide during the VM spike, not up front.

### Placement and grid: `ApplyMonitorsConfig`

`place_output(evdi, x, y)` re-reads the current state, then rebuilds the full logical-monitor list:

- the droppix output moved to (x, y);
- every other monitor unchanged, keeping position, scale, transform, primary and current mode.

It applies that list with method **1 (temporary)**. A pure builder, `mutter_apply_args(state, changes) -> std::string`, produces the `gdbus` argument text and is unit-tested.

Mutter rejects layouts with gaps or overlaps. `grid_position` already produces edge-adjacent cells from the primary's top-left. If Mutter still rejects one, log it and leave GNOME's own placement. (Mutter places new monitors to the right of the existing layout by default.)

### Mirror

GNOME mirrors by putting two monitors into **one logical monitor**, which requires both to run the **same resolution**. KWin, by contrast, scales the primary into droppix's native mode. droppix's EDID currently advertises only the tablet's resolution, so there is normally no common mode.

**Our call (flagged for the user):** in mirror mode on GNOME, droppix also advertises the primary's current resolution in the evdi EDID. Mirror then joins the two at that resolution. The tablet receives the primary's picture at the primary's resolution, scaled to fit (letterboxed if the aspect ratio differs). If no common mode can be formed, mirror falls back to extend and logs why.

### Coordinates the streamer passes on

`set_geometry` takes the droppix rectangle and the desktop bounds in layout pixels. Touch itself is mapped by Mutter (gsettings), so the touch device's 0..65535 range still covers the whole droppix screen regardless of scale.

## Testing

**Unit (TDD):**
- `parse_mutter_state` against captured `GetCurrentState` text: single monitor, two monitors, scaled, rotated, mirrored (two connectors in one logical monitor);
- serial-based identification;
- `mutter_apply_args` for place and mirror;
- `select_backend_kind` with the new input;
- per-session device ids.

**On-device:** two VMs on this PC (KVM/QEMU, evdi from the distro), with the Nexus 10 over Wi-Fi:

- **Ubuntu 24.04 LTS (GNOME 46):** evdi via `evdi-dkms`.
- **Fedora 43 Workstation (GNOME 49):** evdi from a third-party akmod repo.

The checklist on each:
1. picture;
2. touch on the right screen;
3. stylus;
4. two-finger right-click at the tap point;
5. extend placement;
6. grid placement;
7. mirror;
8. rotate the tablet;
9. a second session (simulated with the desktop client if only one tablet is available).

**Regression:** re-run the full host suite (currently 388), and re-check KDE on this PC with the Nexus.

**Spike first:** before writing the backend, boot the Ubuntu VM and confirm three things manually: Mutter adopts the evdi output; the gsettings `output` mapping moves a uinput touchscreen; and how absolute-pointer motion is mapped. If any of these is false, come back to this design before building.

## Docs on ship

- STATUS rows "Cross-desktop beyond KWin" and "Sway / GNOME Wayland backends".
- The README Requirements desktop list.
- ARCHITECTURE (new backend).
- This spec's Status line.
- The cross-desktop roadmap's M3 line.
- A lesson for any GNOME gotcha found.

## Left to the builder

- The per-session product-id scheme.
- How tolerant the serial match is (decimal vs hex formatting).
- The GVariant parser's internal structure.
- Retry/settle timing for the gsettings write. KWin's binder retries for about 3 s while the device appears.
- Whether stale gsettings paths are cleaned up.
