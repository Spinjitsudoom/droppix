# On-screen keyboard (Android soft keyboard → host)

**Date:** 2026-07-13
**Status:** Shipped on master.
**Roadmap:** tier T2 "On-screen keyboard" (CLIENT). Depends on "Keyboard input" (shipped) — reuses its `Key`/`sendKey` path.

## Summary

Let the Android tablet type on the host **without a physical keyboard** by showing the system soft keyboard and feeding its input into the existing keyboard path. An overlay button toggles the soft keyboard; a custom `InputConnection` on `GlDisplayView` intercepts committed characters and deletions and translates them to **evdev keycodes** (+ Shift), sending them through the same `KeyListener → client.sendKey` path built for physical keyboards. **Android-only. No wire or host change.**

## Why an InputConnection

Android soft keyboards (IMEs) mostly commit **text** through `InputConnection.commitText`, not raw `KeyEvent`s. So capturing them means acting as a text editor and translating committed characters back to evdev keycodes. This is the reverse of the physical-keyboard `KeyMap`, and — like the physical path — it assumes the host applies a US-QWERTY-compatible layout (host layout decides the final character).

**Rejected alternatives:** `inputType = TYPE_NULL` "dumb-keyboard" mode (asks the IME to emit raw KeyEvents — Gboard and most modern IMEs ignore it; unreliable). A new `Text` wire message typed host-side via xdotool (diverges from the evdev/`Key` path, adds a host dependency, needs a protocol change).

## Decisions

| Question | Decision |
| --- | --- |
| Toggle | An overlay keyboard `Button` next to the existing Settings overlay button; tap shows the soft keyboard, tap again (or Back) hides it. |
| Capture | Custom `KeyInputConnection : BaseInputConnection` on `GlDisplayView`; `onCheckIsTextEditor() = true`. |
| Per-character delivery | `inputType = TYPE_CLASS_TEXT or TYPE_TEXT_VARIATION_VISIBLE_PASSWORD or TYPE_TEXT_FLAG_NO_SUGGESTIONS` — disables predictive **composing**, so the IME calls `commitText` per character (no batching / mid-word composing edits to reconcile). |
| Translation | `CharMap.toEvdev(c) → (evdev, needsShift)?` (US-QWERTY); emit `Shift↓ (if needed) → key↓ → key↑ → Shift↑` via the existing `keyListener`. |
| Backspace/Delete | `deleteSurroundingText(before, after)` → emit `KEY_BACKSPACE` ×before, `KEY_DELETE` ×after. |
| Enter/Del as events | `sendKeyEvent` already routes to the existing `onKeyDown/onKeyUp` (KeyMap path) — no extra handling. |
| Window resize | `softInputMode = adjustNothing` so the keyboard overlays without resizing/panning the GL surface. |
| Scope | Android-only; printable US-ASCII + Enter/Backspace/Tab/Space. No emoji, autocorrect, non-Latin, gesture typing. No wire/host change. |

## UI (`activity_stream.xml`, `StreamActivity`)

- Add an overlay keyboard `Button` `@+id/kbd_overlay_btn` to the topmost `FrameLayout` in `activity_stream.xml`, positioned beside `settings_overlay_btn` (same style; a "⌨"/"Keyboard" label).
- `StreamActivity`:
  - `kbd_overlay_btn.setOnClickListener { toggleSoftKeyboard() }`.
  - `toggleSoftKeyboard()`: an `imm = getSystemService(InputMethodManager)`; track `imeShown`. If not shown: `surfaceView.requestFocus(); imm.showSoftInput(surfaceView, 0)`. If shown: `imm.hideSoftInputFromWindow(surfaceView.windowToken, 0)`. Flip `imeShown`. (Show/hide are idempotent, so a desync from a Back-dismiss self-corrects on the next tap.)
  - Set `window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING)` in `onCreate` (or `android:windowSoftInputMode="adjustNothing"` on the activity in the manifest).

## Capture (`GlDisplayView`)

- `override fun onCheckIsTextEditor(): Boolean = true`.
- `override fun onCreateInputConnection(outAttrs: EditorInfo): InputConnection`:
  - `outAttrs.inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS`
  - `outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI or EditorInfo.IME_FLAG_NO_FULLSCREEN` (avoid the fullscreen extract editor overtaking the stream in landscape).
  - return `KeyInputConnection(this, false)`.
- `inner class KeyInputConnection(view, fullEditor) : BaseInputConnection(view, fullEditor)`:
  - `commitText(text, newCursorPosition)`: for each `char` in `text`, `typeChar(char)`; return `true`.
  - `deleteSurroundingText(before, after)`: emit `KEY_BACKSPACE` down/up `before` times and `KEY_DELETE` down/up `after` times via `keyListener`; return `true`.
  - (Rely on `BaseInputConnection`'s default `sendKeyEvent` → the view's `onKeyDown/onKeyUp` for IMEs that send Enter/Del as events.)
- `private fun typeChar(c: Char)`: `val (evdev, shift) = CharMap.toEvdev(c) ?: return; val l = keyListener ?: return; if (shift) l.onKey(42, 1); l.onKey(evdev, 1); l.onKey(evdev, 0); if (shift) l.onKey(42, 0)` (`42` = `KEY_LEFTSHIFT`).

## `CharMap` (standalone, unit-testable — like `KeyMap`)

`object CharMap { fun toEvdev(c: Char): Pair<Int, Boolean>? }` — returns `(evdevKeycode, needsShift)` or `null` for unmapped (US-QWERTY):

- `'a'..'z'` → `(KEY_<letter>, false)`; `'A'..'Z'` → `(KEY_<letter>, true)` (reuse the letter→evdev values from `KeyMap`).
- `'1'..'9','0'` → digits `(2..11, false)`; shifted `!@#$%^&*()` → same digit keycodes with `true`.
- Symbols (unshifted / shifted share a key): `-`/`_`→MINUS(12); `=`/`+`→EQUAL(13); `[`/`{`→LEFTBRACE(26); `]`/`}`→RIGHTBRACE(27); `\`/`|`→BACKSLASH(43); `;`/`:`→SEMICOLON(39); `'`/`"`→APOSTROPHE(40); `` ` ``/`~`→GRAVE(41); `,`/`<`→COMMA(51); `.`/`>`→DOT(52); `/`/`?`→SLASH(53). Shifted variant → `true`.
- `' '`→SPACE(57,false); `'\n'`→ENTER(28,false); `'\t'`→TAB(15,false).
- Anything else (emoji, accented, non-Latin) → `null` (skipped).

## Testing

- **`CharMap.toEvdev`** unit test (standalone JVM, like `KeyMapTest`): `'a'→(30,false)`, `'A'→(30,true)`, `'z'→(44,false)`, `'1'→(2,false)`, `'!'→(2,true)`, `')'→(11,true)`, `'/'→(53,false)`, `'?'→(53,true)`, `' '→(57,false)`, `'\n'→(28,false)`, unmapped `'é'→null`, `'😀'→null`.
- **On-device:** tap the overlay keyboard button → soft keyboard appears over the stream; typing letters/digits/symbols produces the right characters on the host; **Shift**/capitals and shifted symbols work; **Backspace** deletes; **Enter** newlines; the GL surface isn't resized/panned; tap again (or Back) hides the keyboard; the physical-keyboard path still works unchanged.

## Out of scope

- Emoji, autocorrect/predictive/gesture typing, non-Latin scripts (can't map to evdev keycodes).
- Layout configuration (host layout decides the character; US-QWERTY assumption, same as the physical path).
- The Linux client (already has a physical keyboard). No wire/host/protocol change.
