# Mouse input — scroll wheel + right/middle buttons

**Date:** 2026-07-12
**Status:** Shipped on master.
**Roadmap:** tier T1 "Mouse input" (sub-project B of the T1 remainder).

## Summary

Complete the physical-mouse experience on both clients: a **scroll wheel** and **direct right/middle buttons**. Cursor movement and left-click already work through the existing touch path and are left unchanged.

The host already has an "aux pointer" uinput device (absolute `ABS_X/Y` + `BTN_LEFT`/`BTN_RIGHT`) used today only for the two-finger-tap→right-click gesture. We **generalize that device into a full mouse** (add `BTN_MIDDLE`, `REL_WHEEL`, `REL_HWHEEL`) and route physical-mouse events to it via two new client→host messages. Finger-driven touch (multitouch device) and the two-finger-tap right-click gesture are untouched.

## Decisions

| Question | Decision |
| --- | --- |
| Scope | Full: scroll (vertical + horizontal) + right + middle buttons. |
| Host device | Generalize the existing aux pointer device (not a new device). |
| Movement / left-click | Unchanged — stays on the touch/multitouch path. |
| Linux right-click | Replace the "synthesize a two-finger tap" hack with a direct `BTN_RIGHT`; keep middle new. |
| Android finger right-click | Keep the two-finger-tap gesture (coexists with real mouse buttons). |
| Position handling | Each mouse message carries the pointer x/y, so the action lands where the mouse is — no continuous hover-move tracking needed. |

## Protocol (`host/src/protocol.{h,cpp}` + Kotlin `Protocol.kt`)

Two new message types (client → host), added to `MsgType`:

- **`Scroll = 12`** — body: `i16 dx, i16 dy, u16 x, u16 y` (big-endian). `dx`/`dy` are signed wheel **clicks** (horizontal/vertical; +y = scroll up/away, +x = right); `x`/`y` are the pointer position normalized to 0..65535.
  - `encode_scroll(int dx, int dy, uint16 x, uint16 y)` / `decode_scroll(body, dx, dy, x, y)`.
- **`MouseButton = 13`** — body: `u8 button, u8 action, u16 x, u16 y`. `button`: `1`=right, `2`=middle. `action`: `0`=up, `1`=down.
  - `encode_mouse_button(uint8 button, uint8 action, uint16 x, uint16 y)` / `decode_mouse_button(...)`.

No change to `Hello`/`Touch`/`Input`/`Orientation`. (These messages don't ride HELLO — they're independent control messages, so no protocol-version bump.)

## Host (`input_injector.{h,cpp}`, `transport_server.{h,cpp}`, `stream_daemon.cpp`)

- **Generalize the aux pointer device** (the `rc_fd_` device set up in `InputInjector`): additionally `UI_SET_KEYBIT BTN_MIDDLE`, `UI_SET_EVBIT EV_REL`, `UI_SET_RELBIT REL_WHEEL`, `UI_SET_RELBIT REL_HWHEEL`.
- **New methods** (reusing the existing `x_norm`/`y_norm` → desk scaling that `right_click` already does):
  - `void scroll(int dx, int dy, uint16_t x_norm, uint16_t y_norm)` — emit `ABS_X`/`ABS_Y` (scaled), then `REL_HWHEEL dx`, `REL_WHEEL dy`, `SYN_REPORT`.
  - `void mouse_button(uint8_t button, bool down, uint16_t x_norm, uint16_t y_norm)` — emit `ABS_X`/`ABS_Y` (scaled), then `EV_KEY` `BTN_RIGHT`/`BTN_MIDDLE` (down/up), `SYN_REPORT`.
  - `right_click` stays (used by the gesture path).
- **Transport handlers:** `transport_server` gains `set_scroll_handler(...)` and `set_mouse_button_handler(...)`, dispatched in `poll_control` when a `Scroll`/`MouseButton` message arrives (same shape as the existing touch/orientation handlers).
- **Wiring:** `stream_daemon` sets those handlers to call `injector.scroll(...)` / `injector.mouse_button(...)` — only when the injector is active (evdi + `cfg_.touch`, same gate as touch injection; otherwise ignore, like touch).

## Android client (`GlDisplayView.kt`)

Physical-mouse events arrive with source `SOURCE_MOUSE`:
- **Scroll:** override `onGenericMotionEvent`; on `ACTION_SCROLL`, read `AXIS_VSCROLL`/`AXIS_HSCROLL`, round to integer clicks, and send a `Scroll` with the pointer x/y normalized (same normalizer as touch).
- **Right/middle buttons:** in the existing `onTouchEvent` (or `onGenericMotionEvent`) mouse path, read `buttonState`; on a `BUTTON_SECONDARY`/`BUTTON_TERTIARY` press/release edge, send `MouseButton` (right/middle, down/up, normalized x/y). Left-button drag continues to flow through the touch path unchanged.

## Linux client (`gui/video_widget.{h,cpp}`)

- **Scroll:** override `wheelEvent`; convert `QWheelEvent::angleDelta()` (±120/detent) to signed clicks (`/120`), send `Scroll` with the normalized cursor position.
- **Right/middle:** in `mousePressEvent`/`mouseReleaseEvent`, on `Qt::RightButton`/`Qt::MiddleButton` send `MouseButton` (right/middle, down/up, normalized) — **removing** the current right-click-as-two-finger-tap synthesis. Left button stays on the existing touch-contact synthesis.
- The `TouchCallback` interface gains sibling callbacks (or the widget gets `ScrollCallback`/`MouseButtonCallback`) so `main_window` forwards these to the transport as `Scroll`/`MouseButton` messages.

## Testing

- **Protocol:** `encode/decode_scroll` and `encode/decode_mouse_button` round-trip tests (C++ `test_protocol` + Kotlin `ProtocolTest`) — signed deltas, button/action, x/y.
- **Host:** the `InputInjector` writes real uinput events (needs `/dev/uinput` + root), so — like the existing touch injection — its emission isn't unit-tested; it's verified on-device. What IS unit-tested: the protocol round-trips above, plus (if the injector exposes any pure normalization of `x_norm`/`y_norm`→desk coords) that scaling. Keep the injector change minimal and mirror the proven `right_click` emit pattern so the on-device check is the gate.
- **On-device:** mouse wheel scrolls the desktop under the pointer; right-button = right-click; middle-button = middle-click; two-finger-tap still right-clicks (finger, no mouse); left-click/drag unaffected.

## Out of scope

- Cursor hover-move tracking (position rides each event instead).
- Horizontal-scroll on devices that don't report it (sent only when `AXIS_HSCROLL`/`angleDelta().x()` is non-zero).
- Keyboard input, stylus (separate roadmap items).
- No HELLO/protocol-version change (these are independent control messages).
