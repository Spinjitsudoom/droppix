# Touch Input (finger → cursor) — Design

**Date:** 2026-06-24
**Status:** Shipped on master.
**Scope:** Phase 2 of the original plan — a touch input back-channel so finger
touches on the Android tablet drive the cursor/clicks on the virtual monitor.
Finger touch only; stylus pressure is a later phase.

## Goal

Let a finger touch on the tablet act as a direct, absolute pointer on the droppix
virtual monitor: tap = click at that point, drag = move + drag. The tablet sends
normalized touch events to the host; the host injects them via a `uinput` virtual
pointer mapped onto the virtual monitor's place in the desktop.

## Decisions (settled during brainstorming)

| Decision | Choice |
|---|---|
| Scope | **Finger touch only** now; stylus + pressure is a follow-up phase |
| Interaction | **Direct/absolute, single pointer**: tap = click where tapped, drag = move+drag (no multi-touch/gestures in v1) |
| Host injection | **A: `uinput` absolute pointer** spanning the whole desktop; normalized tap mapped into the droppix monitor's rectangle (geometry from `kscreen-doctor`). No udev rules |
| Protocol | New **`INPUT` message (type 7), app→host**, additive — an old host ignores the unknown type |
| Privilege | `uinput` needs root; the evdi streamer already runs as root (`pkexec`), so injection rides in that process. Touch works in the **evdi session**; the unprivileged test-pattern session stays display-only |

## Protocol addition (additive, byte-identical both ends)

`enum MsgType { ..., Input = 7 }` (app→host only). Body (big-endian, 5 bytes):
- `u8 action` — 0 = down, 1 = move, 2 = up
- `u16 x_norm` — 0..65535 = normalized 0..1 across the displayed video width
- `u16 y_norm` — 0..65535 = normalized 0..1 across the displayed video height

`encode_input(action,x,y)` / `decode_input(body)` added to the C++ `protocol`
(host/src/protocol.*) and the Kotlin `Protocol` (android/.../protocol/Protocol.kt),
kept byte-identical and cross-checked. No HELLO version bump needed: the change is
additive and an older peer simply skips an unknown message type.

## Android side

```
finger → DisplaySurfaceView.onTouchEvent → normalize → TransportClient.sendInput → socket
```

| Unit | Responsibility |
|---|---|
| `DisplaySurfaceView.onTouchEvent` | Single-pointer `MotionEvent` (DOWN/MOVE/UP): `x_norm = clamp01(event.x / width) * 65535`, `y_norm = clamp01(event.y / height) * 65535`; invoke a touch callback with (action, x_norm, y_norm). Secondary pointers ignored (single-pointer v1). |
| `TransportClient.sendInput(action, xNorm, yNorm)` | Serialize an `INPUT` message and write it to the socket. **Thread-safe:** touch events arrive on the UI thread while the read loop runs on the net thread, so all socket writes are guarded by a single lock. No-op if not connected. |
| `MainActivity` | Set the surface's touch callback to `client.sendInput(...)` for the active client. |

Normalization is relative to the SurfaceView bounds. With the resolution presets
matching the Nexus 10's 16:10, there is no letterbox; pixel-exact mapping under
mismatched-aspect modes is a later refinement (taps still map to the right
relative point).

## Host side

```
TransportServer reads INPUT → InputInjector.inject(action,x_norm,y_norm)
   → map norm → desktop pixel on the droppix monitor → uinput ABS_X/Y + BTN_LEFT
```

| Unit | Responsibility | Tested |
|---|---|---|
| `InputInjector` (new, host/src) | Open `/dev/uinput`; create an absolute pointer (`ABS_X`/`ABS_Y` 0–65535, `BTN_LEFT`). `inject(action,x_norm,y_norm)`: on down → set ABS + `BTN_LEFT=1` + SYN; move → ABS + SYN; up → `BTN_LEFT=0` + SYN. | mapping math unit; device operator |
| `map_to_abs` (pure fn) | `(x_norm,y_norm, monitor_rect, desktop_bounds) → (abs_x,abs_y)` in 0..65535: `global = monitor_offset + norm/65535 * monitor_size`, `abs = global / desktop_size * 65535`. | **unit** |
| `MonitorGeometry` (pure parser, new) | Parse `kscreen-doctor -o` text into outputs (name + geometry x,y,w,h); select the droppix monitor (by EDID name "droppix" if present, else the enabled output whose size matches the configured mode); compute the desktop bounding box (max of x+w, y+h over enabled outputs). | **unit** (sample text) |
| `TransportServer` (extend) | In the control-read path, parse `INPUT` messages and dispatch to a callback, alongside the existing PING→PONG. | — |
| `StreamDaemon` (extend, evdi path) | After `wait_for_mode`: run `kscreen-doctor -o`, parse via `MonitorGeometry`, construct `InputInjector` with the droppix rect + desktop bounds, and connect `TransportServer` INPUT → `injector.inject`. | — |

The droppix monitor advertises EDID monitor name "droppix" (descriptor #2), which
aids identification; the size-match fallback covers cases where the name isn't
surfaced. The `uinput` device's `ABS` range is a fixed 0–65535 logical space that
libinput maps proportionally across the whole desktop, so `map_to_abs` outputs
that space directly.

## Privilege & sessions

- `uinput` requires root. The evdi streamer is launched via `pkexec` (root), so
  `InputInjector` opens `/dev/uinput` in that process — no new privilege path.
- Touch injection is active only in the **evdi (root) session**. The test-pattern
  session is unprivileged and display-only; it ignores `INPUT` (no injector).

## Error handling

- `/dev/uinput` open fails → log once, run **display-only** (no injector, stream
  unaffected).
- Droppix output not found in the layout → log, skip injector (display-only)
  rather than mis-injecting.
- Unknown `INPUT` action byte → ignored.
- `sendInput` while disconnected → no-op on the app side.

## Testing strategy

- **Pure unit (GoogleTest):** `encode_input`/`decode_input` round-trip; `map_to_abs`
  for representative monitor/desktop layouts (incl. a monitor offset within a
  multi-output desktop); `MonitorGeometry` parse of a sample `kscreen-doctor -o`
  block → correct droppix rect + desktop bounds; selection by name and by
  size-fallback.
- **Pure unit (JUnit):** Kotlin `Protocol` INPUT codec round-trip, byte-identical
  to the host (assert exact bytes, like the existing protocol tests).
- **Build-gate:** `InputInjector` (uinput) and the Android touch path compile/link;
  no behavioral unit test for the device/UI (runtime types).
- **Operator (live, evdi session):** touch the tablet → the cursor moves to that
  point on the droppix monitor; tap = click; drag = drag; verify in a real app.

## Out of scope (this phase)

- Stylus / pen pressure (next phase; same back-channel, adds a tablet device with
  `ABS_PRESSURE` + pen tool-type).
- Multi-touch gestures (two-finger scroll, pinch/zoom).
- Pixel-exact mapping under letterboxed mismatched-aspect modes.
- Touch injection in the test-pattern session (unprivileged).
- Keyboard input.
