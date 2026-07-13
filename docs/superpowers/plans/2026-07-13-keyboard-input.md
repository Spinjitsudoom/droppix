# Keyboard Input Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Forward a physical/Bluetooth keyboard's keystrokes from either client (Android, Linux Qt) to the host, so the extended display can be typed on — via a new `Key` control message and a host uinput keyboard device.

**Architecture:** Each client maps its native key event to a **Linux evdev keycode** and sends `{keycode, action}` over a new `Key = 14` message. The host owns a new uinput keyboard device and replays `EV_KEY`. Mirrors the existing "client normalizes, host replays" model already used for touch/scroll/mouse buttons. Modifiers/shortcuts fall out as separate down/up events.

**Tech Stack:** C++ (host + Linux client, Qt6), Kotlin/Android, CMake + Gradle.

## Global Constraints

- **No HELLO/version bump.** `Key` is an independent control message like Touch/Scroll/MouseButton. Do NOT touch `kProtocolVersion` / `Protocol.VERSION` / `encode_hello`.
- **Message numbering:** append `Key = 14` after `MouseButton = 13`. Do NOT renumber Touch(11)/Scroll(12)/MouseButton(13).
- **Wire format (must match C++ ↔ Kotlin exactly):** `Key` body = `u16 keycode` (big-endian) then `u8 action`. `action`: `0`=up, `1`=down, `2`=repeat. `keycode` = Linux evdev code.
- **evdev keycodes:** the client is the source of the evdev code. Linux client uses `nativeScanCode() - 8` (X11). Android uses the `KeyMap.toEvdev` table in Task 5 — copy those numbers verbatim.
- **Gating:** key injection is wired only when the injector is active (the existing `cfg_.touch && have_output` block), and the handler is reset to `nullptr` at session start alongside the other handlers.
- **Do NOT swallow Android system keys** (Back/Home/volume): `onKeyDown`/`onKeyUp` return `super.…` for any key that maps to `0`.
- **Build/test envs** (repo on CIFS no-exec mount):
  - Host: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build <-R filter> --output-on-failure'` (configure once with `cmake -S host -B ~/droppix-build -DDROPPIX_BUILD_TESTS=ON` if `~/droppix-build` is missing).
  - Linux client: `distrobox enter droppix-dev -- bash -lc 'cmake -S client -B ~/droppix-client-build -DDROPPIX_CLIENT_BUILD_TESTS=ON && cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build <-R filter> --output-on-failure'`
  - Android: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew <task>'` — `sh gradlew` (not `./gradlew`), set `ANDROID_HOME`.
- Work on branch `feat/keyboard-input` (off `master`). Commit after each task.

---

### Task 1: C++ protocol — `Key = 14` + encode/decode

**Files:**
- Modify: `host/src/protocol.h`, `host/src/protocol.cpp`
- Test: `host/tests/test_protocol.cpp`

**Interfaces:**
- Produces: `MsgType::Key = 14`; `std::vector<unsigned char> encode_key(uint16_t keycode, uint8_t action)`; `bool decode_key(const std::vector<unsigned char>& body, uint16_t& keycode, uint8_t& action)`.

- [ ] **Step 1: Write the failing tests** — add to `host/tests/test_protocol.cpp` (next to the scroll/mouse-button round-trips)

```cpp
TEST(Protocol, KeyRoundTrip) {
  auto b = droppix::encode_key(300, 1);          // 300 proves u16 (KEY_* can exceed 255)
  ASSERT_EQ(b.size(), 3u);
  uint16_t kc; uint8_t a;
  ASSERT_TRUE(droppix::decode_key(b, kc, a));
  EXPECT_EQ(kc, 300); EXPECT_EQ(a, 1);
  auto b2 = droppix::encode_key(30, 2);           // KEY_A, repeat
  ASSERT_TRUE(droppix::decode_key(b2, kc, a));
  EXPECT_EQ(kc, 30); EXPECT_EQ(a, 2);
}
TEST(Protocol, KeyShortBodyRejected) {
  std::vector<unsigned char> tooShort{0x01, 0x2C};   // 2 bytes, need 3
  uint16_t kc; uint8_t a;
  EXPECT_FALSE(droppix::decode_key(tooShort, kc, a));
}
```
(Use whatever namespace/qualification the neighbouring scroll tests use — if they call `encode_scroll` unqualified, drop the `droppix::`.)

- [ ] **Step 2: Run to verify FAIL**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build -R "Protocol.Key" --output-on-failure'`
Expected: FAIL (undefined `encode_key`/`decode_key`).

- [ ] **Step 3: Implement.**
  - `protocol.h`: in `enum class MsgType`, append `, Key = 14` after `MouseButton = 13`. Declare near `encode_mouse_button`:
    ```cpp
    std::vector<unsigned char> encode_key(uint16_t keycode, uint8_t action);
    bool decode_key(const std::vector<unsigned char>& body, uint16_t& keycode, uint8_t& action);
    ```
  - `protocol.cpp` (next to `encode_scroll`):
    ```cpp
    std::vector<unsigned char> encode_key(uint16_t keycode, uint8_t action) {
      return { (unsigned char)(keycode >> 8), (unsigned char)(keycode & 0xFF), action };
    }
    bool decode_key(const std::vector<unsigned char>& b, uint16_t& keycode, uint8_t& action) {
      if (b.size() < 3) return false;
      keycode = (uint16_t)((b[0] << 8) | b[1]); action = b[2];
      return true;
    }
    ```

- [ ] **Step 4: Run to verify PASS** — same as Step 2. Expected: PASS (both Key tests).

- [ ] **Step 5: Commit**

```bash
git add host/src/protocol.h host/src/protocol.cpp host/tests/test_protocol.cpp
git commit -m "feat(host/protocol): encode_key/decode_key + MsgType::Key"
```

---

### Task 2: Host injector — uinput keyboard device + `key()`

**Files:**
- Modify: `host/src/input_injector.h`, `host/src/input_injector.cpp`

**Interfaces:**
- Produces: `void InputInjector::key(uint16_t keycode, uint8_t action)`.

- [ ] **Step 1: Read the existing device setup.** In `input_injector.cpp`, read `open()` (creates `fd_`, the multitouch device) and the `rc_fd_` creation block in `set_geometry` — mirror their `ioctl(UI_SET_*)` → `UI_DEV_SETUP` → `UI_DEV_CREATE` shape and the free `emit(fd, type, code, val)` helper. Read `~InputInjector` (destroys `fd_`/`rc_fd_`).

- [ ] **Step 2: Implement.**
  - `input_injector.h`: add public `void key(uint16_t keycode, uint8_t action);` and private `int kb_fd_ = -1;` (next to `rc_fd_`).
  - `input_injector.cpp`, at the END of `open()` — after `fd_`'s `UI_DEV_CREATE` succeeds and BEFORE `return true;` — create the keyboard device (non-fatal on failure; keyboard is optional, touch stays primary):
    ```cpp
    kb_fd_ = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (kb_fd_ >= 0) {
      ioctl(kb_fd_, UI_SET_EVBIT, EV_KEY);
      for (int code = 1; code < 256; ++code) ioctl(kb_fd_, UI_SET_KEYBIT, code);
      struct uinput_setup kus{};
      kus.id.bustype = BUS_USB; kus.id.vendor = 0x1209; kus.id.product = 0xD303;
      std::snprintf(kus.name, sizeof(kus.name), "droppix-keyboard");
      if (ioctl(kb_fd_, UI_DEV_SETUP, &kus) < 0 || ioctl(kb_fd_, UI_DEV_CREATE) < 0) {
        std::fprintf(stderr, "keyboard: uinput create failed; disabled\n");
        ::close(kb_fd_); kb_fd_ = -1;
      }
    } else {
      std::fprintf(stderr, "keyboard: uinput open failed; disabled\n");
    }
    ```
    (Match the exact `uinput_setup`/`snprintf` style used by the `rc_fd_` block — reuse its local variable pattern. Pick a vendor/product distinct from the touch and aux devices.)
  - Add the `key()` method (near `scroll`/`mouse_button`):
    ```cpp
    void InputInjector::key(uint16_t keycode, uint8_t action) {
      if (kb_fd_ < 0) return;
      emit(kb_fd_, EV_KEY, keycode, action);
      emit(kb_fd_, EV_SYN, SYN_REPORT, 0);
    }
    ```
  - In `~InputInjector`, add alongside the `rc_fd_` teardown: `if (kb_fd_ >= 0) { ioctl(kb_fd_, UI_DEV_DESTROY); ::close(kb_fd_); }`.

- [ ] **Step 3: Build (injector uinput emission isn't unit-tested — verified on-device in Task 7)**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j 2>&1 | tail -4'`
Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add host/src/input_injector.h host/src/input_injector.cpp
git commit -m "feat(host): uinput keyboard device + InputInjector::key"
```

---

### Task 3: Host dispatch + daemon wiring

**Files:**
- Modify: `host/src/transport_server.h`, `host/src/transport_server.cpp`, `host/src/stream_daemon.cpp`
- Test: `host/tests/test_transport_server.cpp`

**Interfaces:**
- Consumes: `MsgType::Key`, `decode_key` (Task 1); `InputInjector::key` (Task 2).
- Produces: `TransportServer::set_key_handler(std::function<void(uint16_t, uint8_t)>)`.

- [ ] **Step 1: Write the failing test** — add to `host/tests/test_transport_server.cpp` (mirror `ScrollHandlerFires`; reuse its fake channel + `poll_control` setup)

```cpp
TEST(TransportServer, KeyHandlerFires) {
  // (mirror ScrollHandlerFires: build a server over the fake channel, register the handler,
  //  feed one Key message, poll, assert.)
  uint16_t gotKc = 0; uint8_t gotAction = 0; int calls = 0;
  server.set_key_handler([&](uint16_t kc, uint8_t a){ gotKc = kc; gotAction = a; ++calls; });
  feed(encode_message(MsgType::Key, encode_key(30, 1)));   // KEY_A down
  server.poll_control();
  EXPECT_EQ(calls, 1); EXPECT_EQ(gotKc, 30); EXPECT_EQ(gotAction, 1);
}
```
(Match the exact fixture names/helpers `ScrollHandlerFires` uses — `server`, `feed`, `encode_message`, etc. If that test constructs its server inline, do the same here.)

- [ ] **Step 2: Run to verify FAIL**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build -R "TransportServer.KeyHandlerFires" --output-on-failure'`
Expected: FAIL (no `set_key_handler`).

- [ ] **Step 3: Implement.**
  - `transport_server.h`: add a setter next to `set_mouse_button_handler`:
    ```cpp
    // Called for each KEY message during poll_control with (keycode, action).
    // Same lifetime invariant as the touch handler.
    void set_key_handler(std::function<void(uint16_t, uint8_t)> h) { key_handler_ = std::move(h); }
    ```
    and a member next to `mouse_button_handler_`: `std::function<void(uint16_t, uint8_t)> key_handler_;`
  - `transport_server.cpp`, in `poll_control`, after the `MouseButton` branch:
    ```cpp
    } else if (m.type == MsgType::Key && key_handler_) {
      uint16_t kc; uint8_t a;
      if (decode_key(m.body, kc, a)) key_handler_(kc, a);
    }
    ```
  - `stream_daemon.cpp`: add `tx_.set_key_handler(nullptr);` next to the other `set_*_handler(nullptr)` resets; and inside the `cfg_.touch` (have_output) injector block, after `set_mouse_button_handler`:
    ```cpp
    tx_.set_key_handler([&injector](uint16_t kc, uint8_t a) {
      injector.key(kc, a);
    });
    ```

- [ ] **Step 4: Run to verify PASS + full host suite**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build --output-on-failure 2>&1 | tail -8'`
Expected: `KeyHandlerFires` passes; full host suite green (no regressions).

- [ ] **Step 5: Commit**

```bash
git add host/src/transport_server.h host/src/transport_server.cpp host/src/stream_daemon.cpp host/tests/test_transport_server.cpp
git commit -m "feat(host): dispatch Key -> injector.key (handler + daemon wiring)"
```

---

### Task 4: Kotlin protocol — `KEY(14)` + `encodeKey`

**Files:**
- Modify: `android/app/src/main/java/com/droppix/app/protocol/Protocol.kt`
- Test: `android/app/src/test/java/com/droppix/app/protocol/ProtocolTest.kt`

**Interfaces:**
- Produces: `MsgType.KEY(14)`; `Protocol.encodeKey(keycode: Int, action: Int): ByteArray`.

- [ ] **Step 1: Write the failing test** — add to `ProtocolTest.kt` (next to the scroll test)

```kotlin
@Test fun encodeKeyMatchesWire() {
    val b = Protocol.encodeKey(300, 2)     // 300 = u16 (0x012C), action 2
    assertEquals(3, b.size)
    assertEquals(0x01.toByte(), b[0]); assertEquals(0x2C.toByte(), b[1]); assertEquals(0x02.toByte(), b[2])
}
```

- [ ] **Step 2: Run to verify FAIL**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest --tests "*ProtocolTest*"'`
Expected: FAIL (unresolved `encodeKey`).

- [ ] **Step 3: Implement.**
  - In `enum class MsgType`, append `, KEY(14)` after `MOUSE_BUTTON(13)`.
  - Add (next to `encodeScroll`):
    ```kotlin
    // KEY body: u16 keycode (big-endian), u8 action (0=up,1=down,2=repeat).
    fun encodeKey(keycode: Int, action: Int): ByteArray =
        byteArrayOf((keycode ushr 8).toByte(), keycode.toByte(), action.toByte())
    ```

- [ ] **Step 4: Run to verify PASS** — same as Step 2. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/protocol/Protocol.kt android/app/src/test/java/com/droppix/app/protocol/ProtocolTest.kt
git commit -m "feat(android/protocol): encodeKey + MsgType.KEY"
```

---

### Task 5: Android capture + send

**Files:**
- Create: `android/app/src/main/java/com/droppix/app/ui/KeyMap.kt`
- Modify: `android/app/src/main/java/com/droppix/app/net/TransportClient.kt`
- Modify: `android/app/src/main/java/com/droppix/app/ui/GlDisplayView.kt`
- Modify: `android/app/src/main/java/com/droppix/app/ui/StreamActivity.kt`
- Test: `android/app/src/test/java/com/droppix/app/ui/KeyMapTest.kt`

**Interfaces:**
- Consumes: `Protocol.encodeKey`, `MsgType.KEY` (Task 4).
- Produces: `KeyMap.toEvdev(keyCode: Int): Int`; `TransportClient.sendKey(keycode: Int, action: Int)`; `GlDisplayView.KeyListener` + `setKeyListener`.

- [ ] **Step 1: Write the failing test** — create `KeyMapTest.kt`

```kotlin
package com.droppix.app.ui
import android.view.KeyEvent
import org.junit.Assert.assertEquals
import org.junit.Test
class KeyMapTest {
    @Test fun mapsCommonKeys() {
        assertEquals(30, KeyMap.toEvdev(KeyEvent.KEYCODE_A))        // KEY_A
        assertEquals(28, KeyMap.toEvdev(KeyEvent.KEYCODE_ENTER))    // KEY_ENTER
        assertEquals(29, KeyMap.toEvdev(KeyEvent.KEYCODE_CTRL_LEFT))// KEY_LEFTCTRL
        assertEquals(2,  KeyMap.toEvdev(KeyEvent.KEYCODE_1))        // KEY_1
        assertEquals(57, KeyMap.toEvdev(KeyEvent.KEYCODE_SPACE))    // KEY_SPACE
    }
    @Test fun unmappedReturnsZero() {
        assertEquals(0, KeyMap.toEvdev(KeyEvent.KEYCODE_VOLUME_UP))
    }
}
```
(`KeyEvent.KEYCODE_*` are compile-time `static final int` constants, so this runs as a plain JVM unit test without an Android runtime — that is WHY `toEvdev` lives in a standalone `object`, not on the `View`.)

- [ ] **Step 2: Run to verify FAIL**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest --tests "*KeyMapTest*"'`
Expected: FAIL (unresolved `KeyMap`).

- [ ] **Step 3: Implement.**
  - `KeyMap.kt` — a standalone object (evdev numbers verbatim):
    ```kotlin
    package com.droppix.app.ui
    import android.view.KeyEvent
    // Android KeyEvent.keyCode -> Linux evdev keycode. 0 = unmapped (caller passes the event through).
    object KeyMap {
        fun toEvdev(keyCode: Int): Int = when (keyCode) {
            KeyEvent.KEYCODE_A -> 30; KeyEvent.KEYCODE_B -> 48; KeyEvent.KEYCODE_C -> 46
            KeyEvent.KEYCODE_D -> 32; KeyEvent.KEYCODE_E -> 18; KeyEvent.KEYCODE_F -> 33
            KeyEvent.KEYCODE_G -> 34; KeyEvent.KEYCODE_H -> 35; KeyEvent.KEYCODE_I -> 23
            KeyEvent.KEYCODE_J -> 36; KeyEvent.KEYCODE_K -> 37; KeyEvent.KEYCODE_L -> 38
            KeyEvent.KEYCODE_M -> 50; KeyEvent.KEYCODE_N -> 49; KeyEvent.KEYCODE_O -> 24
            KeyEvent.KEYCODE_P -> 25; KeyEvent.KEYCODE_Q -> 16; KeyEvent.KEYCODE_R -> 19
            KeyEvent.KEYCODE_S -> 31; KeyEvent.KEYCODE_T -> 20; KeyEvent.KEYCODE_U -> 22
            KeyEvent.KEYCODE_V -> 47; KeyEvent.KEYCODE_W -> 17; KeyEvent.KEYCODE_X -> 45
            KeyEvent.KEYCODE_Y -> 21; KeyEvent.KEYCODE_Z -> 44
            KeyEvent.KEYCODE_1 -> 2; KeyEvent.KEYCODE_2 -> 3; KeyEvent.KEYCODE_3 -> 4
            KeyEvent.KEYCODE_4 -> 5; KeyEvent.KEYCODE_5 -> 6; KeyEvent.KEYCODE_6 -> 7
            KeyEvent.KEYCODE_7 -> 8; KeyEvent.KEYCODE_8 -> 9; KeyEvent.KEYCODE_9 -> 10
            KeyEvent.KEYCODE_0 -> 11
            KeyEvent.KEYCODE_GRAVE -> 41; KeyEvent.KEYCODE_MINUS -> 12; KeyEvent.KEYCODE_EQUALS -> 13
            KeyEvent.KEYCODE_LEFT_BRACKET -> 26; KeyEvent.KEYCODE_RIGHT_BRACKET -> 27
            KeyEvent.KEYCODE_BACKSLASH -> 43; KeyEvent.KEYCODE_SEMICOLON -> 39
            KeyEvent.KEYCODE_APOSTROPHE -> 40; KeyEvent.KEYCODE_COMMA -> 51
            KeyEvent.KEYCODE_PERIOD -> 52; KeyEvent.KEYCODE_SLASH -> 53
            KeyEvent.KEYCODE_SPACE -> 57; KeyEvent.KEYCODE_ENTER -> 28
            KeyEvent.KEYCODE_DEL -> 14; KeyEvent.KEYCODE_FORWARD_DEL -> 111
            KeyEvent.KEYCODE_TAB -> 15; KeyEvent.KEYCODE_ESCAPE -> 1
            KeyEvent.KEYCODE_SHIFT_LEFT -> 42; KeyEvent.KEYCODE_SHIFT_RIGHT -> 54
            KeyEvent.KEYCODE_CTRL_LEFT -> 29; KeyEvent.KEYCODE_CTRL_RIGHT -> 97
            KeyEvent.KEYCODE_ALT_LEFT -> 56; KeyEvent.KEYCODE_ALT_RIGHT -> 100
            KeyEvent.KEYCODE_META_LEFT -> 125; KeyEvent.KEYCODE_META_RIGHT -> 126
            KeyEvent.KEYCODE_CAPS_LOCK -> 58
            KeyEvent.KEYCODE_DPAD_UP -> 103; KeyEvent.KEYCODE_DPAD_DOWN -> 108
            KeyEvent.KEYCODE_DPAD_LEFT -> 105; KeyEvent.KEYCODE_DPAD_RIGHT -> 106
            KeyEvent.KEYCODE_MOVE_HOME -> 102; KeyEvent.KEYCODE_MOVE_END -> 107
            KeyEvent.KEYCODE_PAGE_UP -> 104; KeyEvent.KEYCODE_PAGE_DOWN -> 109
            KeyEvent.KEYCODE_INSERT -> 110
            KeyEvent.KEYCODE_F1 -> 59; KeyEvent.KEYCODE_F2 -> 60; KeyEvent.KEYCODE_F3 -> 61
            KeyEvent.KEYCODE_F4 -> 62; KeyEvent.KEYCODE_F5 -> 63; KeyEvent.KEYCODE_F6 -> 64
            KeyEvent.KEYCODE_F7 -> 65; KeyEvent.KEYCODE_F8 -> 66; KeyEvent.KEYCODE_F9 -> 67
            KeyEvent.KEYCODE_F10 -> 68; KeyEvent.KEYCODE_F11 -> 87; KeyEvent.KEYCODE_F12 -> 88
            else -> 0
        }
    }
    ```
  - `TransportClient.kt` — add next to `sendScroll`:
    ```kotlin
    fun sendKey(keycode: Int, action: Int) {
        val o = out ?: return
        val msg = Protocol.encodeMessage(MsgType.KEY, Protocol.encodeKey(keycode, action))
        submitSend(o, msg)
    }
    ```
    (Match the exact `out`/output-stream retrieval that `sendScroll` uses.)
  - `GlDisplayView.kt`:
    - In the `init {}` block, add `isFocusableInTouchMode = true`.
    - Add (next to the `MouseListener` interface / `setMouseListener`):
      ```kotlin
      interface KeyListener { fun onKey(keycode: Int, action: Int) }
      @Volatile private var keyListener: KeyListener? = null
      fun setKeyListener(l: KeyListener?) { keyListener = l }
      ```
    - Override key events:
      ```kotlin
      override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
          val e = KeyMap.toEvdev(keyCode)
          if (e == 0) return super.onKeyDown(keyCode, event)
          keyListener?.onKey(e, if (event.repeatCount > 0) 2 else 1)
          return true
      }
      override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
          val e = KeyMap.toEvdev(keyCode)
          if (e == 0) return super.onKeyUp(keyCode, event)
          keyListener?.onKey(e, 0)
          return true
      }
      ```
      (Add `import android.view.KeyEvent` if not present.)
  - `StreamActivity.kt` — mirror the `setMouseListener` wiring:
    - In `onResume` (where `setMouseListener` is set, ~line 123): add
      ```kotlin
      surfaceView.setKeyListener(object : GlDisplayView.KeyListener {
          override fun onKey(keycode: Int, action: Int) { client?.sendKey(keycode, action) }
      })
      surfaceView.requestFocus()
      ```
    - In `onPause` (where `setMouseListener(null)` is, ~line 147): add `surfaceView.setKeyListener(null)`.

- [ ] **Step 4: Run to verify PASS + build**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest --tests "*KeyMapTest*" assembleDebug 2>&1 | tail -6'`
Expected: `KeyMapTest` passes; `BUILD SUCCESSFUL`.

- [ ] **Step 5: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/ui/KeyMap.kt android/app/src/main/java/com/droppix/app/net/TransportClient.kt android/app/src/main/java/com/droppix/app/ui/GlDisplayView.kt android/app/src/main/java/com/droppix/app/ui/StreamActivity.kt android/app/src/test/java/com/droppix/app/ui/KeyMapTest.kt
git commit -m "feat(android): capture hardware keyboard -> sendKey (evdev map)"
```

---

### Task 6: Linux client capture + send

**Files:**
- Modify: `client/gui/video_widget.h`, `client/gui/video_widget.cpp`
- Modify: `client/src/transport_client.h`, `client/src/transport_client.cpp`
- Modify: `client/gui/main_window.cpp`

**Interfaces:**
- Consumes: `MsgType::Key`, `encode_key` (Task 1).
- Produces: `VideoWidget::setKeyCallback`; `TransportClient::sendKey(uint16_t keycode, uint8_t action)`.

- [ ] **Step 1: Read the existing scroll/mouse-button plumbing** in `video_widget.{h,cpp}` (the `ScrollCallback`/`scrollCb_` + `wheelEvent` pattern) and `transport_client.cpp` `sendScroll`, and the callback wiring in `main_window.cpp` (`setScrollCallback([this]…{ if (client_) client_->sendScroll(...) })`). Mirror them.

- [ ] **Step 2: Implement.**
  - `video_widget.h`:
    - add `using KeyCallback = std::function<void(uint16_t keycode, uint8_t action)>;` and `void setKeyCallback(KeyCallback cb) { keyCb_ = std::move(cb); }` (next to `setScrollCallback`);
    - add protected overrides `void keyPressEvent(QKeyEvent* e) override;` and `void keyReleaseEvent(QKeyEvent* e) override;`;
    - add member `KeyCallback keyCb_;`.
  - `video_widget.cpp`:
    - in the constructor, set focus so the widget receives keys: `setFocusPolicy(Qt::StrongFocus);`
    - implement (X11 scancode = evdev + 8):
      ```cpp
      void VideoWidget::keyPressEvent(QKeyEvent* e) {
        int sc = static_cast<int>(e->nativeScanCode());
        if (sc < 9) { e->ignore(); return; }         // no usable evdev code
        if (keyCb_) keyCb_(static_cast<uint16_t>(sc - 8), e->isAutoRepeat() ? 2 : 1);
        e->accept();
      }
      void VideoWidget::keyReleaseEvent(QKeyEvent* e) {
        if (e->isAutoRepeat()) { e->accept(); return; }   // autorepeat release is an artifact, not a real up
        int sc = static_cast<int>(e->nativeScanCode());
        if (sc < 9) { e->ignore(); return; }
        if (keyCb_) keyCb_(static_cast<uint16_t>(sc - 8), 0);
        e->accept();
      }
      ```
      (Add `#include <QKeyEvent>` if not already included.)
  - `transport_client.h`: declare `void sendKey(uint16_t keycode, uint8_t action);` (next to `sendScroll`).
  - `transport_client.cpp` — mirror `sendScroll`:
    ```cpp
    void TransportClient::sendKey(uint16_t keycode, uint8_t action) {
      std::lock_guard<std::mutex> lk(sendLock_);
      if (!connected_) return;                       // match sendScroll's connected/guard check exactly
      auto msg = encode_message(MsgType::Key, encode_key(keycode, action));
      send_all(msg);
    }
    ```
    (Use whatever guard + send call `sendScroll` uses — copy its body shape verbatim, swapping the encode call.)
  - `main_window.cpp` — next to the `setScrollCallback`/`setMouseButtonCallback` wiring (~lines 63-68):
    ```cpp
    video_->setKeyCallback([this](uint16_t kc, uint8_t a) {
      if (client_) client_->sendKey(kc, a);
    });
    ```

- [ ] **Step 3: Build + full client suite**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build --output-on-failure 2>&1 | tail -8'`
Expected: clean build; client suite green (no regressions).

- [ ] **Step 4: Commit**

```bash
git add client/gui/video_widget.h client/gui/video_widget.cpp client/src/transport_client.h client/src/transport_client.cpp client/gui/main_window.cpp
git commit -m "feat(client): capture keyboard -> sendKey (nativeScanCode)"
```

---

### Task 7: Verification

**Files:** none.

- [ ] **Step 1: Full builds + suites**

```
distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build --output-on-failure'
distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build --output-on-failure'
distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest assembleDebug'
```
Expected: host suite green (incl. `Protocol.KeyRoundTrip`, `TransportServer.KeyHandlerFires`); client suite green; Android unit tests green (incl. `KeyMapTest`); APK builds.

- [ ] **Step 2: On-device.** With the injector active (touch-enabled session): type on the tablet's Bluetooth keyboard and in the Linux client window → characters appear on the extended display; `Ctrl+C`/`Ctrl+V`, arrow keys, `Backspace`, `Shift`+letter (capitals), and `Enter`/`Tab` all behave; Android system keys (Back/Home/volume) still work; autorepeat (hold a key) repeats.

- [ ] **Step 3: Commit any fixes; otherwise done.**

---

## Self-review notes

- **Spec coverage:** protocol (T1/T4), host device+dispatch (T2/T3), Android capture (T5), Linux capture (T6), tests per-task + T7. Every spec section maps to a task.
- **Wire consistency:** C++ `encode_key` = `{kc>>8, kc&0xFF, action}`; Kotlin `encodeKey` = `byteArrayOf(kc ushr 8, kc, action)` — identical 3-byte layout; both tests assert `[0x01,0x2C,0x02]`-shaped bytes for keycode 300.
- **No version bump:** `Key = 14` / `KEY(14)` appended; HELLO untouched (Global Constraints).
- **Type consistency:** `key(uint16_t,uint8_t)`, `set_key_handler(void(uint16_t,uint8_t))`, `sendKey(uint16_t,uint8_t)` / `sendKey(Int,Int)`, `KeyMap.toEvdev(Int):Int` — consistent.
- **evdev map sanity:** letters/digits/symbols/mods/nav/function verified against `linux/input-event-codes.h` (KEY_A=30, KEY_1=2, KEY_0=11, KEY_ENTER=28, KEY_LEFTCTRL=29, KEY_SPACE=57, KEY_LEFTMETA=125). Android `KEYCODE_DEL`→`KEY_BACKSPACE(14)`, `KEYCODE_FORWARD_DEL`→`KEY_DELETE(111)`.
- **System-key safety:** unmapped → `0` → `super.onKey*` (Back/Home/volume pass through).
- **X11 assumption** (Linux client `nativeScanCode()-8`) is per the spec's noted limitation.
