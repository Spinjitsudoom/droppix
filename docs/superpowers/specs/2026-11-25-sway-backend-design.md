# Sway (wlroots) backend (M2)

**Date:** 2026-11-25
**Status:** Shipped on master (2026-11-25). Approved by delegation (user asleep: "feel free to make the necessary decisions").
**Update (build):** VM testing found that placing only the droppix output made Sway re-arrange the unpinned main screen to its right, drifting further each session. `sway_place_command` pins the other active outputs in the same command. Verified with `wev`: touch (640,400)/(320,200), right button, pen at 75 %/75 %, three back-to-back reconnects.
**Roadmap:** M2 of [`2026-07-05-cross-desktop-portability-design.md`](2026-07-05-cross-desktop-portability-design.md). Second of the four desktop projects, after GNOME ([design](2026-11-25-gnome-wayland-backend-design.md)).

## Intent

Full droppix support on Sway, for users of Sway or of distros/spins that ship it (Fedora Sway, Manjaro Sway, …). Hyprland is the other big wlroots-family compositor; it uses a different IPC (`hyprctl`) and is a follow-up backend in the same shape.

## What the VM spike found (Ubuntu 24.04, Sway 1.9)

- **Sway adopts the evdi output on its own**, placing it to the right with droppix's EDID serial, *if* wlroots can render for evdi. evdi has no render node, so wlroots needs `WLR_EVDI_RENDER_DEVICE=/dev/dri/renderDNNN` in Sway's environment (a documented wlroots variable). Without it: "Failed to create DRM backend" and the screen never appears. droppix can't set a compositor's environment, so this is a **user setup step**. The streamer logs a hint when Sway never modes the output.
- **Detection bug:** the "is the session X11?" probe under pkexec only checked `wayland-0`. Sway was on `wayland-1`, and XWayland's `/tmp/.X11-unix/X0` existed, so droppix chose the X11 backend and bound touch via `xinput`, which can't see uinput devices under XWayland. Fix: any live `wayland-N` socket (with its lock) means Wayland, using the existing `pick_wayland_display` logic.

## Design

`SwayBackend : DesktopBackend`, all through `swaymsg` (JSON, `-r`) with `SWAYSOCK` added to the session prefix:

| Operation | How |
|---|---|
| Detect | `XDG_CURRENT_DESKTOP` contains "sway", or a live `sway-ipc.<uid>.<pid>.sock` in the user's runtime dir |
| `outputs()` | `swaymsg -t get_outputs -r` → name, active, rect (layout px, already scaled) |
| `find_output(serial)` | the output whose `serial` matches (hex or decimal) |
| `map_touch` / `map_pen` | `swaymsg -t get_inputs -r` → the input whose `name` is the device's → `swaymsg input <identifier> map_to_output <output>` |
| `place_output` | `swaymsg output <name> pos <x> <y>` |
| `apply_layout(Mirror)` | Unsupported. Sway has no output mirroring. Logs and stays extended. |
| `revive_output` | Logs the `WLR_EVDI_RENDER_DEVICE` hint (Sway never modes the output without it) |

**Socket discovery.** `SWAYSOCK` is `/run/user/<uid>/sway-ipc.<uid>.<pid>.sock`. Pick the one whose pid is alive; stale sockets from earlier Sway runs stay behind.

**JSON.** A small pure JSON parser (`json_value.{h,cpp}`) with tests. No new dependencies.

**Serial matching** is shared with GNOME: a pure `edid_serial_matches(text, serial)`.

## Non-goals

- Mirror on Sway (no compositor support).
- Setting `WLR_EVDI_RENDER_DEVICE` for the user.
- Hyprland, river, labwc, Wayfire in this change.

## Testing

- **Unit:** JSON parser; `parse_sway_outputs`; `parse_sway_inputs` identifier lookup; socket pick; detection (Sway, and Wayland-on-`wayland-1` not mistaken for X11); serial matching.
- **VM:** the GNOME rig with a Sway autologin. Run the same scripted taps, two-finger tap, pen and placement, plus two tablets.
