# Keyboard input — physical keyboard client → host

**Date:** 2026-07-13
**Status:** Shipped on master.
**Roadmap:** tier T2 "Keyboard input" (BOTH). Unblocks the later T2 "On-screen keyboard" item.

## Summary

Forward a **physical / Bluetooth keyboard**'s keystrokes from either client (Android tablet, Linux Qt client) to the host, so the extended display can be typed on. Each client maps its native key event to a **Linux evdev keycode** and sends `{keycode, action}`; the host replays it through a new uinput **keyboard** device. This mirrors the existing input model (client normalizes, host replays) already used for touch, scroll, and mouse buttons. **The Android on-screen soft keyboard is a separate later item and is out of scope here.**

## Decisions

| Question | Decision |
| --- | --- |
| Wire format | **evdev keycodes** on the wire (client maps native → `KEY_*`). Host stays trivial and layout-correct; modifiers/shortcuts fall out as separate down/up events. |
| Message | New `Key = 14` control message; body `u16 keycode, u8 action`. No HELLO/version bump (independent control message, like Touch/Scroll/MouseButton). |
| action values | `0` = up, `1` = down, `2` = repeat (matches evdev `EV_KEY` values). |
| Host device | A new uinput keyboard device in `InputInjector` (separate from the touch/aux-pointer devices). |
| Layout | Host keyboard layout decides the character (client sends physical/semantic keycodes). WYSIWYG when client and host layouts match. |
| Gating | Injected only when the injector is active (same evdi + `cfg_.touch` gate as touch). |
| Scope | Physical/BT keyboard capture on **both** clients. On-screen soft keyboard, host→client keys, and layout remap config are out of scope. |

## Protocol (`host/src/protocol.{h,cpp}` + Kotlin `Protocol.kt`)

New message type appended to `MsgType` (C++ enum + Kotlin):

- **`Key = 14`** — body: `u16 keycode, u8 action` (big-endian). `keycode` = Linux evdev code (e.g. `KEY_A` = 30); `action`: `0`=up, `1`=down, `2`=repeat.
  - C++: `std::vector<unsigned char> encode_key(uint16_t keycode, uint8_t action);` / `bool decode_key(const std::vector<unsigned char>& body, uint16_t& keycode, uint8_t& action);` (guard `body.size() < 3`).
  - Kotlin: `fun encodeKey(keycode: Int, action: Int): ByteArray` — `u16` keycode (big-endian) then `u8` action, byte-identical to the C++ layout.

No change to `Hello`/`Touch`/`Scroll`/`MouseButton`/`Orientation`. `Touch = 11`, `Scroll = 12`, `MouseButton = 13` keep their numbers; `Key` is appended as `14`.

## Host (`input_injector.{h,cpp}`, `transport_server.{h,cpp}`, `stream_daemon.cpp`)

- **New uinput keyboard device** `kb_fd_` in `InputInjector` (alongside `fd_` = multitouch, `rc_fd_` = aux pointer). Setup mirrors the existing device creation: `UI_SET_EVBIT EV_KEY`, then enable the keycode range via `UI_SET_KEYBIT` for codes `1..255` (covers the full standard typing set — letters, digits, symbols, modifiers, `Enter`/`Backspace`/`Tab`/`Space`, arrows/nav, function keys, keypad — all of which are below 256), then `UI_DEV_SETUP`/`UI_DEV_CREATE`. `ok()`/teardown extended to cover it.
- **New method** `void key(uint16_t keycode, uint8_t action)` — guard `kb_fd_ >= 0`; write `EV_KEY <keycode> <action>` then `SYN_REPORT`. No coordinate scaling (keyboard is global). `action` passed straight through as the `EV_KEY` value (0/1/2).
- **Transport handler:** `transport_server` gains `set_key_handler(std::function<void(uint16_t,uint8_t)>)`, dispatched in `poll_control` when a `Key` message arrives (`decode_key` → handler, gated on the decode bool), same shape as the touch/scroll/mouse-button handlers.
- **Wiring:** `stream_daemon` binds `set_key_handler` to `injector.key(...)` inside the same active block as touch/scroll/mouse-button injection (evdi + `cfg_.touch`); resets it to `nullptr` at session start alongside the other handlers.

## Android client (`Protocol.kt`, `TransportClient`, `GlDisplayView`/`StreamActivity`)

- **`TransportClient.sendKey(keycode: Int, action: Int)`** mirroring `sendScroll`/`sendMouseButton` (under the `submitSend` lock).
- **Capture:** override `onKeyDown(keyCode, event)` / `onKeyUp(keyCode, event)` (on `GlDisplayView`, which already handles touch/mouse). A pure helper `androidKeyToEvdev(keyCode: Int): Int` maps Android `KeyEvent` codes to evdev (`KEYCODE_A→KEY_A`, digits, punctuation, `SHIFT_LEFT/RIGHT`, `CTRL_LEFT/RIGHT`, `ALT_LEFT/RIGHT`, `META_LEFT/RIGHT`, `ENTER`, `DEL`(Backspace), `TAB`, `SPACE`, `ESCAPE`, arrows, `HOME/END/PAGE_UP/PAGE_DOWN/INSERT/FORWARD_DEL`, `F1..F12`), returning `0` for unmapped keys.
  - On a mapped key: `sendKey(evdev, if (event.repeatCount > 0) 2 else 1)` on down, `sendKey(evdev, 0)` on up; return `true` (consume).
  - On an unmapped key (return `0`): return `super.onKeyDown/Up(...)` so system keys (Back, Home, volume) are untouched.
- The view already receives key events when focused/streaming; ensure it is focusable (`isFocusableInTouchMode = true`, request focus in `StreamActivity`).

## Linux client (`gui/video_widget.{h,cpp}`, `transport_client`, `main_window.cpp`)

- **`VideoWidget`**: set `Qt::StrongFocus`; override `keyPressEvent`/`keyReleaseEvent`. Compute the evdev code via the pure helper `droppix::scancode_to_evdev(nativeScanCode(), wayland)` (`client/src/keycode_util.h`) and send `{evdev, action}` where press with `event->isAutoRepeat()` → `2` else `1`, release → `0`. `scancode_to_evdev` returns `0` for a non-positive/absurd evdev, which is skipped. Accept the event.
  - **Platform-aware offset:** `scancode_to_evdev` branches on `wayland = QGuiApplication::platformName() == "wayland"` — X11 scancodes are evdev + 8 (subtracted), Wayland scancodes are already raw evdev (no offset). Both X11 and Wayland Qt clients now send correct evdev codes; unit-tested in `tests/test_client_settings.cpp` (`ScancodeToEvdev.*`).
  - A new `KeyCallback` (or `setKeyCallback`) on `VideoWidget`, forwarded by `main_window` to the transport, mirroring `setScrollCallback`/`setMouseButtonCallback`.
- **`transport_client.sendKey(uint16_t keycode, uint8_t action)`** mirroring `sendScroll`/`sendMouseButton` (`sendLock_` + `encode_message` + `send_all`).
- **`main_window`** wires the `VideoWidget` key callback to `client_->sendKey(...)`, like the scroll/mouse-button callbacks (guarded by `if (client_)`).

## Testing

- **Protocol:** `encode/decode_key` round-trip tests (C++ `test_protocol` + Kotlin `ProtocolTest`) — keycode (incl. a value > 255 to prove `u16`), action 0/1/2, and a short-body reject in `decode_key`.
- **Host:** `TransportServer` `KeyHandlerFires` test using the existing fake channel + `poll_control` (feed a `Key` message, assert the handler sees the decoded keycode/action). The `InputInjector` writes real uinput events (needs `/dev/uinput` + root), so — like touch/scroll — its emission is verified **on-device**, not unit-tested; keep the injector change minimal and mirror the proven emit pattern.
- **Android:** a small unit test for `androidKeyToEvdev` (a few representative mappings: `KEYCODE_A→30`, `KEYCODE_ENTER→28`, `KEYCODE_CTRL_LEFT→29`, unmapped→0).
- **On-device:** type on the tablet's Bluetooth keyboard and in the Linux client window → characters appear on the extended display; `Ctrl+C`/`Ctrl+V`, arrow keys, `Backspace`, and modifiers behave correctly; system keys (Back/Home/volume) still work on Android.

## Out of scope

- Android **on-screen soft keyboard** (separate later T2 item; depends on this).
- Host → client key events; global hotkey capture; per-key remapping or layout configuration.
- Media/consumer keys beyond the standard typing/navigation/function set.
