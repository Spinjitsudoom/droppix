# Stylus support (Android pen → host pen device)

**Date:** 2026-07-13
**Status:** Shipped on master.
**Roadmap:** tier T2 "Stylus support" (CLIENT). Reuses the input model (client normalizes, host replays).

## Summary

Forward an Android tablet's **stylus** to the host as a real graphics-tablet pen, so drawing apps (Krita, GIMP) get **pressure** and recognize the **eraser** end. A stylus is distinct from a finger: routing it through the multitouch device would make it a plain touch (no pen tool-type, no pressure semantics), so the host gets a separate uinput **pen device** and the client sends a new `Pen` control message. **Scope: pressure + eraser only — no tilt, no hover. Android-only** (the Linux client is a desktop viewer; unchanged). No HELLO/protocol-version change.

## Decisions

| Question | Decision |
| --- | --- |
| Device model | A separate uinput **pen** device on the host (not the multitouch device), so it's classified as a graphics tablet with `BTN_TOOL_PEN`. |
| Wire | New `Pen = 15` control message (independent, like Touch/Scroll/Key — no HELLO/version bump). |
| Features | **Pressure** (0..1023) + **eraser** end (`BTN_TOOL_RUBBER`). No tilt, no hover. |
| Coordinate mapping | Pen device is `INPUT_PROP_DIRECT` with `ABS_X/Y` 0..65535 and is bound to the droppix output (X11 `xinput map-to-output`; KWin device→output mapping) so strokes land on that monitor. Binding the pen on KWin is the main integration risk (see below). |
| Client | Android only. Finger touch is unchanged. Linux client unchanged. |

## Protocol (`host/src/protocol.{h,cpp}` + Kotlin `Protocol.kt`)

- **`Pen = 15`** — appended to `MsgType` after `Key = 14` (no renumber). Body: `u16 x, u16 y, u16 pressure, u8 flags` (big-endian). `x`/`y` = 0..65535 across the droppix monitor (same normalization as touch); `pressure` = 0..1023; `flags` bit0 = `touching` (contact/down), bit1 = `eraser`.
  - C++: `encode_pen(uint16_t x, uint16_t y, uint16_t pressure, uint8_t flags)` / `decode_pen(body, x, y, pressure, flags)` (guard `size < 7`).
  - Kotlin: `encodePen(x: Int, y: Int, pressure: Int, flags: Int): ByteArray` — byte-identical.

## Host (`input_injector.{h,cpp}`, `transport_server.{h,cpp}`, `stream_daemon.cpp`)

- **New uinput pen device** `pen_fd_` in `InputInjector` (alongside `fd_` = multitouch, `rc_fd_` = aux pointer, `kb_fd_` = keyboard). Setup mirrors the touch device (`INPUT_PROP_DIRECT`, `ABS_X/Y` 0..65535, direct/absolute), with pen caps: `EV_KEY` → `BTN_TOOL_PEN`, `BTN_TOOL_RUBBER`, `BTN_TOUCH`; `EV_ABS` → `ABS_X`, `ABS_Y`, `ABS_PRESSURE` (0..1023). Created in `open()` (non-fatal on failure; touch stays primary). Named `<name>-pen`. Distinct vendor/product id. Destroyed in the destructor.
- **New method** `void pen(uint16_t x, uint16_t y, uint16_t pressure, bool touching, bool eraser)`:
  - On a proximity edge (pen goes from up→down), emit the current tool `EV_KEY` `BTN_TOOL_RUBBER` (eraser) or `BTN_TOOL_PEN` = 1; on down→up, clear it = 0. Track a small `pen_down_`/`pen_eraser_` state to emit clean tool + `BTN_TOUCH` edges.
  - Emit `ABS_X = x`, `ABS_Y = y` (0..65535 direct — the device is bound to the droppix output), `ABS_PRESSURE = pressure`, `BTN_TOUCH = touching`, `SYN_REPORT`.
- **Transport handler:** `transport_server` gains `set_pen_handler(std::function<void(uint16_t,uint16_t,uint16_t,uint8_t)>)`, dispatched in `poll_control` on a `Pen` message (`decode_pen` → handler), same shape as touch/scroll/key.
- **Wiring:** `stream_daemon` binds `set_pen_handler` to `injector.pen(...)` inside the same active/`cfg_.touch` block as touch (reset to `nullptr` at session start with the others).
- **Output binding (main integration risk):** the pen device must be bound to the droppix output so `ABS_X/Y` 0..65535 span it. Add `DesktopBackend::map_pen(output, pen_dev_name)`:
  - **X11**: `xinput map-to-output <pen> <output>` — identical to the touch mapping; works for any absolute device. Reuse the `X11Backend::map_touch` retry/settle logic.
  - **KWin**: the pen is a **tablet**, not a touch device, so KWin's `InputDeviceManager.ListTouch` (used by `map_touch`) won't return it. Locate the pen among **all** input devices (e.g. `ListDevices`/iterate device ids and match `name == <pen>`), then set its `outputName` the same way `map_touch` does for touch. If KWin doesn't expose an output binding for tablet devices, the pen still functions but may land on the wrong monitor — this is the item to **validate on-device first**; if unbindable, fall back to positioning via the desktop-pixel `scale_x/scale_y` path the aux pointer uses.
  - `stream_daemon` calls `backend->map_pen(out_name, <pen_name>)` next to the existing `map_touch` call.

## Android client (`Protocol.kt`, `TransportClient`, `GlDisplayView`)

- **`TransportClient.sendPen(x: Int, y: Int, pressure: Int, flags: Int)`** mirroring `sendScroll`/`sendKey` (under the send lock).
- **Capture:** in `GlDisplayView.onTouchEvent`, for the active pointer read `event.getToolType(idx)`. If `TOOL_TYPE_STYLUS` or `TOOL_TYPE_ERASER`:
  - Compute normalized `x`/`y` with the SAME normalizer the touch path uses; `pressure = (event.getPressure(idx) * 1023).toInt().coerceIn(0, 1023)`; `eraser = toolType == TOOL_TYPE_ERASER`; `touching` = the action is DOWN/MOVE (true) vs UP/CANCEL (false).
  - `flags = (if (touching) 1 else 0) or (if (eraser) 2 else 0)`; `sendPen(x, y, pressure, flags)`; consume the event (return true).
  - Do NOT also route the stylus through the finger `Touch` path (a stylus event must produce pen events only).
- **Fingers** (`TOOL_TYPE_FINGER` / default) continue through the existing touch-contact path unchanged.

## Testing

- **Protocol:** `encode/decode_pen` round-trip tests (C++ `test_protocol` + Kotlin `ProtocolTest`) — x/y/pressure and flags (touching, eraser, both), plus a short-body reject in `decode_pen`.
- **Host:** `transport_server` `PenHandlerFires` test (feed a `Pen` message via the fake channel + `poll_control`, assert the handler sees x/y/pressure/flags). The `InputInjector` writes real uinput events (needs `/dev/uinput` + root), so — like touch — its emission is verified on-device, not unit-tested; keep the pen device/method minimal and mirror the proven touch pattern.
- **On-device:** draw with the tablet's stylus on the extended display → pressure-sensitive strokes on the host (verify in Krita/GIMP: pressure varies line width/opacity); flip the stylus to the eraser end → it erases (`BTN_TOOL_RUBBER`); finger touch still moves the cursor / does multitouch; the pen lands on the droppix monitor (not the host's primary).

## Out of scope

- **Tilt** and **hover** (pen proximity without contact) — a later increment; would add `ABS_TILT_X/Y` + `ABS_DISTANCE` + Android `onHoverEvent` + tilt/orientation conversion.
- **Barrel (stylus) button.**
- The **Linux client** (no common stylus there; QTabletEvent is a separate later item if wanted).
- No HELLO/protocol-version change (independent control message).
