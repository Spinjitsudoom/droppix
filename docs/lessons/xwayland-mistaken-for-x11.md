# L-2026-11-25-xwayland-mistaken-for-x11: Sway on wayland-1 was driven as an X11 session

- **ID:** `L-2026-11-25-xwayland-mistaken-for-x11`
- **Tags:** `host`, `desktop-backend`, `sway`, `wrong-answer`, `high`, `lesson`
- **Date:** 2026-11-25

## Symptom

Under Sway, the streamer logged `[touch-bind] droppix-touch-27000 not found via xinput after
retries`: it had picked the **X11** backend on a Wayland session.

## Root cause

Under pkexec, `XDG_SESSION_TYPE` is gone, so `make_desktop_backend` guessed. It checked only
`/run/user/<uid>/wayland-0`. Sway ran on `wayland-1`, so the check failed, and XWayland's
`/tmp/.X11-unix/X0` made it look like an X11 session. The `xrandr` view under XWayland even
names the outputs correctly, so identification "worked", but `xinput` can't see uinput devices
there, so touch was never bound. This is the same mistake as `hardcoded-wayland-0`, in a second
place.

## Fix

`has_live_wayland_socket(entries)`: any `wayland-N` with its `.lock` means Wayland. It is used in
place of the `wayland-0` check. Unit-tested (`WaylandSession.AnyLiveWaylandSocketMeansWayland`).

## Related

On Sway, positioning only the droppix output (`output DVI-I-1 pos …`) made Sway auto-arrange
the main screen, which had no explicit position, to its right, a further screen-width every
session. `sway_place_command` pins every other active output at its current position in the same
command list.
