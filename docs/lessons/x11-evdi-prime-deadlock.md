# L-2026-07-26-x11-evdi-prime-deadlock: evdi never gets a mode on X11 (Cinnamon/XFCE/etc)

- **ID:** `L-2026-07-26-x11-evdi-prime-deadlock`
- **Tags:** `host`, `evdi`, `desktop-backend`, `silent-failure`, `high`, `lesson`
- **Date:** 2026-07-26
- **Related:** [`2026-07-06-desktop-backend-m1-design.md`](../superpowers/specs/2026-07-06-desktop-backend-m1-design.md), [[zxing-embedded-minsdk]] (unrelated bug, same session)

## Symptom

On X11 desktops (confirmed on Cinnamon; likely XFCE/MATE/GNOME-on-Xorg too),
every connection attempt looped forever:

```
desktop backend: x11
client HELLO v5 2340x1080 ...
opened evdi node 0
evdi: no KWin mode within 5s for 2340x1080@60
source start failed
```

The client (Android) reconnects, the host re-opens the evdi node, and it fails
again — indefinitely. No `[output-adopt]` log lines ever appear. Works fine on
the primary target (KDE Plasma / KWin, Wayland).

## Root cause

`evdi` shows up to X as a **separate GPU/PRIME provider**, not as a new
connector on the existing GPU (confirmed via `xrandr --listproviders`: a
second `Provider ... cap: 0x2, Sink Output`). X will not assign that
provider's output a mode until:

1. it's linked as a reverse-PRIME sink of provider 0
   (`xrandr --setprovideroutputsource <evdi-provider> 0`), and
2. the output is explicitly enabled (`xrandr --output <name> --auto`).

`X11Backend::adopt_output()` already did both steps — but only in
`stream_daemon.cpp`, **after** `EvdiFrameSource::start()` returned. `start()`
itself calls `Capturer::wait_for_mode(5000)`, which blocks for a mode that
only `adopt_output` can produce. So the wait always timed out before the
enable step ever ran — a straightforward ordering deadlock that only exists
on backends where the compositor doesn't auto-mode a hotplugged output.
KWin auto-configures new outputs itself, so the same code path never hit
this timing dependency there — the bug was invisible on the primary dev/test
platform.

## Fix

`host/src/frame_source.h` / `evdi_frame_source.cpp`: `FrameSource::start()`
gained an optional `on_connected` callback, run on a **background thread**
while `wait_for_mode` pumps evdi events on the calling thread (mirrors the
`serviced()` pattern already used later in `stream_daemon.cpp` — calling
xrandr synchronously from the evdi thread would itself deadlock on X's evdi
probe). `DesktopBackend::link_providers()` (new; X11-only override, no-op on
KWin/Generic) runs the provider-link + a bare `--auto` on any
connected-but-modeless output — just enough to satisfy the mode-wait.
Placement (`--right-of` the primary) is left to the existing `adopt_output`,
still called after `start()` returns once the output is identified by name.

```cpp
// stream_daemon.cpp
auto on_connected = [this]{ desktop_->link_providers(); };
if (!src_ || !src_->start(w, h, on_connected)) { ... }
```

## How to detect this in the future

- Any `wait_for_mode`/similar blocking wait that depends on a **separate
  thread or process** performing the action the wait is blocked on is a
  latent deadlock — grep for `wait_for_*` / `wait_readable` calls and check
  whether the corresponding "make it happen" call runs before or after.
- Backend-specific timing assumptions baked in by testing only against one
  compositor (here: KWin's auto-configure) won't surface until tested against
  a compositor that behaves differently (X11 here). When adding a new
  `DesktopBackend`, re-walk the *ordering* of every existing blocking call in
  the shared path (`stream_daemon.cpp`), not just its per-backend methods.
- `xrandr --listproviders` showing more than one `Provider` line is the
  giveaway that evdi is a reverse-PRIME sink on this machine, not a connector
  on the primary GPU.
