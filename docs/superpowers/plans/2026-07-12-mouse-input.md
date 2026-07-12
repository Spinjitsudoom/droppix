# Mouse Input (scroll + right/middle buttons) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a scroll wheel and direct right/middle mouse buttons to both clients, injected on the host via its generalized aux-pointer uinput device.

**Architecture:** Two new client→host control messages (`Scroll`, `MouseButton`), each carrying the pointer x/y. The host's existing aux-pointer device (already `ABS_X/Y` + `BTN_LEFT/RIGHT`) gains `BTN_MIDDLE` + `REL_WHEEL`/`REL_HWHEEL` and two handler methods. Cursor movement + left-click stay on the touch path unchanged; no HELLO/protocol-version bump.

**Tech Stack:** C++17 (host + Linux client, CMake, uinput), Kotlin/Android, Qt6.

## Global Constraints

- **Two new `MsgType`s: `Scroll = 12`, `MouseButton = 13`** (C++ `MsgType` enum + Kotlin `MsgType`). These are independent control messages — **no HELLO/version change**.
- **Wire formats** (big-endian):
  - `Scroll`: `i16 dx, i16 dy, u16 x, u16 y` (8 bytes). `dx`/`dy` = signed wheel clicks (+y up/away, +x right); `x`/`y` = pointer 0..65535.
  - `MouseButton`: `u8 button, u8 action, u16 x, u16 y` (6 bytes). `button`: `1`=right, `2`=middle. `action`: `0`=up, `1`=down.
- **Host device:** generalize the existing aux-pointer device (do NOT add a new one). Scroll/buttons ride the SAME gate as touch injection (evdi output present + `cfg_.touch`); otherwise ignored, like touch.
- **Movement + left-click unchanged** (touch/multitouch path). The host-side two-finger-tap→right-click gesture stays (for Android finger users).
- **Build/test environments** (repo on a CIFS no-exec mount):
  - Host: `distrobox enter droppix-dev -- bash -lc 'cmake -S host -B ~/droppix-build && cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build <-R filter> --output-on-failure'`
  - Linux client: `distrobox enter droppix-dev -- bash -lc 'cmake -S client -B ~/droppix-client-build -DDROPPIX_CLIENT_BUILD_TESTS=ON && cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build <-R filter> --output-on-failure'`
  - Android: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew <task>'` — `sh gradlew` (not `./gradlew`), set `ANDROID_HOME`.
- Work on branch `feat/mouse-input` (off `master`). Commit after each task.

---

### Task 1: C++ protocol — Scroll + MouseButton

**Files:**
- Modify: `host/src/protocol.h` (MsgType + decls), `host/src/protocol.cpp` (encode/decode)
- Test: `host/tests/test_protocol.cpp`

**Interfaces:**
- Produces:
  - `MsgType::Scroll = 12`, `MsgType::MouseButton = 13`.
  - `std::vector<unsigned char> encode_scroll(int16_t dx, int16_t dy, uint16_t x, uint16_t y);`
  - `bool decode_scroll(const std::vector<unsigned char>& body, int16_t& dx, int16_t& dy, uint16_t& x, uint16_t& y);`
  - `std::vector<unsigned char> encode_mouse_button(uint8_t button, uint8_t action, uint16_t x, uint16_t y);`
  - `bool decode_mouse_button(const std::vector<unsigned char>& body, uint8_t& button, uint8_t& action, uint16_t& x, uint16_t& y);`

- [ ] **Step 1: Write failing tests** — add to `host/tests/test_protocol.cpp`

```cpp
TEST(Protocol, ScrollRoundTrip) {
  auto b = droppix::encode_scroll(-3, 5, 1000, 2000);
  int16_t dx, dy; uint16_t x, y;
  ASSERT_TRUE(droppix::decode_scroll(b, dx, dy, x, y));
  EXPECT_EQ(dx, -3); EXPECT_EQ(dy, 5); EXPECT_EQ(x, 1000); EXPECT_EQ(y, 2000);
  EXPECT_FALSE(droppix::decode_scroll({0,1,2}, dx, dy, x, y));   // too short
}
TEST(Protocol, MouseButtonRoundTrip) {
  auto b = droppix::encode_mouse_button(2, 1, 1234, 5678);
  uint8_t btn, act; uint16_t x, y;
  ASSERT_TRUE(droppix::decode_mouse_button(b, btn, act, x, y));
  EXPECT_EQ(btn, 2); EXPECT_EQ(act, 1); EXPECT_EQ(x, 1234); EXPECT_EQ(y, 5678);
}
```

- [ ] **Step 2: Run to verify FAIL**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S host -B ~/droppix-build && cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build -R Protocol --output-on-failure'`
Expected: FAIL (undefined encode/decode).

- [ ] **Step 3: Implement.** Add to the `MsgType` enum: `Scroll = 12, MouseButton = 13`. In `protocol.cpp` (self-contained big-endian, or use the file's existing put/get u16 helpers if present):

```cpp
std::vector<unsigned char> encode_scroll(int16_t dx, int16_t dy, uint16_t x, uint16_t y) {
  std::vector<unsigned char> b;
  auto u16 = [&](uint16_t v){ b.push_back((unsigned char)(v >> 8)); b.push_back((unsigned char)(v & 0xFF)); };
  u16((uint16_t)dx); u16((uint16_t)dy); u16(x); u16(y);
  return b;
}
bool decode_scroll(const std::vector<unsigned char>& b, int16_t& dx, int16_t& dy, uint16_t& x, uint16_t& y) {
  if (b.size() < 8) return false;
  auto u16 = [&](size_t o){ return (uint16_t)((b[o] << 8) | b[o+1]); };
  dx = (int16_t)u16(0); dy = (int16_t)u16(2); x = u16(4); y = u16(6);
  return true;
}
std::vector<unsigned char> encode_mouse_button(uint8_t button, uint8_t action, uint16_t x, uint16_t y) {
  return { button, action, (unsigned char)(x >> 8), (unsigned char)(x & 0xFF),
           (unsigned char)(y >> 8), (unsigned char)(y & 0xFF) };
}
bool decode_mouse_button(const std::vector<unsigned char>& b, uint8_t& button, uint8_t& action, uint16_t& x, uint16_t& y) {
  if (b.size() < 6) return false;
  button = b[0]; action = b[1];
  x = (uint16_t)((b[2] << 8) | b[3]); y = (uint16_t)((b[4] << 8) | b[5]);
  return true;
}
```
Add matching declarations to `protocol.h`.

- [ ] **Step 4: Run to verify PASS** — same as Step 2. Expected: PASS (+ all existing protocol tests).

- [ ] **Step 5: Commit**

```bash
git add host/src/protocol.h host/src/protocol.cpp host/tests/test_protocol.cpp
git commit -m "feat(protocol): Scroll + MouseButton control messages"
```

---

### Task 2: Host `InputInjector` — generalized aux pointer (scroll + middle)

**Files:**
- Modify: `host/src/input_injector.h`, `host/src/input_injector.cpp`

**Interfaces:**
- Produces: `void InputInjector::scroll(int dx, int dy, uint16_t x_norm, uint16_t y_norm);` and `void InputInjector::mouse_button(uint8_t button, bool down, uint16_t x_norm, uint16_t y_norm);`

- [ ] **Step 1: Read the aux-device setup + `right_click`.** In `input_injector.cpp`, find the aux/right-click device setup (the block with `UI_SET_KEYBIT, BTN_LEFT` / `BTN_RIGHT` on `rc_fd_`, ~lines 104-112) and the `right_click(x_norm, y_norm)` method. Note the EXACT `x_norm`/`y_norm` → `ABS_X`/`ABS_Y` scaling `right_click` uses (it maps 0..65535 to the desk width/height) and the `emit(rc_fd_, ...)` helper.

- [ ] **Step 2: Extend the aux device setup** — right after the `BTN_RIGHT` line:

```cpp
ioctl(rc_fd_, UI_SET_KEYBIT, BTN_MIDDLE);
ioctl(rc_fd_, UI_SET_EVBIT, EV_REL);
ioctl(rc_fd_, UI_SET_RELBIT, REL_WHEEL);
ioctl(rc_fd_, UI_SET_RELBIT, REL_HWHEEL);
```

- [ ] **Step 3: Add the two methods** (declare in `.h`; define in `.cpp`). Reuse the SAME `x_norm`→`ABS_X` scaling `right_click` uses (call it `scale_x(x_norm)`/`scale_y(y_norm)` — factor `right_click`'s scaling into small private helpers if it's inline, so all three share it):

```cpp
void InputInjector::scroll(int dx, int dy, uint16_t x_norm, uint16_t y_norm) {
  if (rc_fd_ < 0) return;
  emit(rc_fd_, EV_ABS, ABS_X, scale_x(x_norm));
  emit(rc_fd_, EV_ABS, ABS_Y, scale_y(y_norm));
  if (dx) emit(rc_fd_, EV_REL, REL_HWHEEL, dx);
  if (dy) emit(rc_fd_, EV_REL, REL_WHEEL, dy);
  emit(rc_fd_, EV_SYN, SYN_REPORT, 0);
}
void InputInjector::mouse_button(uint8_t button, bool down, uint16_t x_norm, uint16_t y_norm) {
  if (rc_fd_ < 0) return;
  int code = (button == 2) ? BTN_MIDDLE : BTN_RIGHT;   // 1=right, 2=middle
  emit(rc_fd_, EV_ABS, ABS_X, scale_x(x_norm));
  emit(rc_fd_, EV_ABS, ABS_Y, scale_y(y_norm));
  emit(rc_fd_, EV_KEY, code, down ? 1 : 0);
  emit(rc_fd_, EV_SYN, SYN_REPORT, 0);
}
```
(If `right_click` doesn't already have reusable `scale_x`/`scale_y`, extract them from its body verbatim so behavior is identical, and have `right_click` call them too.)

- [ ] **Step 4: Build (host lib compiles; no unit test — uinput needs /dev/uinput + root, verified on-device in Task 7)**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j 2>&1 | tail -5'`
Expected: builds clean.

- [ ] **Step 5: Commit**

```bash
git add host/src/input_injector.h host/src/input_injector.cpp
git commit -m "feat(host): aux pointer gains scroll wheel + middle button"
```

---

### Task 3: Host transport dispatch + daemon wiring

**Files:**
- Modify: `host/src/transport_server.h`, `host/src/transport_server.cpp` (handlers + `poll_control` dispatch)
- Modify: `host/src/stream_daemon.cpp` (wire handlers → injector)
- Test: `host/tests/test_transport_server.cpp`

**Interfaces:**
- Consumes: `decode_scroll`/`decode_mouse_button` (Task 1), `injector.scroll`/`injector.mouse_button` (Task 2).
- Produces: `void set_scroll_handler(std::function<void(int16_t,int16_t,uint16_t,uint16_t)>);` and `void set_mouse_button_handler(std::function<void(uint8_t,uint8_t,uint16_t,uint16_t)>);` on `TransportServer`.

- [ ] **Step 1: Read the touch-handler pattern.** In `transport_server.{h,cpp}`, read `set_touch_handler` + how `poll_control` decodes a `MsgType::Touch` message and calls the handler. Mirror it exactly for the two new message types.

- [ ] **Step 2: Write the failing test** — add to `host/tests/test_transport_server.cpp`, using the SAME channel-pairing/setup as the existing tests. Send a `Scroll` message and assert the handler fires with the decoded values:

```cpp
TEST(TransportServer, ScrollHandlerFires) {
  // ... set up the paired channel / TransportServer exactly like the existing touch test ...
  int16_t gdx=0, gdy=0; uint16_t gx=0, gy=0; bool fired=false;
  srv.set_scroll_handler([&](int16_t dx, int16_t dy, uint16_t x, uint16_t y){
    gdx=dx; gdy=dy; gx=x; gy=y; fired=true; });
  // client writes: encode_message(MsgType::Scroll, encode_scroll(-2, 4, 500, 600))
  // ... pump poll_control once ...
  EXPECT_TRUE(fired); EXPECT_EQ(gdx,-2); EXPECT_EQ(gdy,4); EXPECT_EQ(gx,500); EXPECT_EQ(gy,600);
}
```
(Follow the existing touch/hello test's exact mechanism for writing a framed message into the channel and driving `poll_control`.)

- [ ] **Step 3: Run to verify FAIL**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build -R TransportServer --output-on-failure'`
Expected: FAIL (no `set_scroll_handler`).

- [ ] **Step 4: Implement.**
- `transport_server.h`: add the two handler members + setters (mirror the touch handler's `std::function` member + `set_*_handler`).
- `transport_server.cpp` `poll_control`: add cases — on `MsgType::Scroll`, `decode_scroll(body, dx,dy,x,y)` then `if (scroll_handler_) scroll_handler_(dx,dy,x,y);`; on `MsgType::MouseButton`, `decode_mouse_button(...)` then call `mb_handler_`.
- `stream_daemon.cpp`: where `set_touch_handler` is wired to the injector (inside the `cfg_.touch` + have_output block), add:
```cpp
tx_.set_scroll_handler([&injector](int16_t dx, int16_t dy, uint16_t x, uint16_t y){ injector.scroll(dx, dy, x, y); });
tx_.set_mouse_button_handler([&injector](uint8_t b, uint8_t a, uint16_t x, uint16_t y){ injector.mouse_button(b, a != 0, x, y); });
```
Reset them to `nullptr` at session start alongside `set_touch_handler(nullptr)` (so a stale handler from a prior session's injector isn't called).

- [ ] **Step 5: Run to verify PASS + full host suite**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build --output-on-failure'`
Expected: full host suite PASS.

- [ ] **Step 6: Commit**

```bash
git add host/src/transport_server.h host/src/transport_server.cpp host/src/stream_daemon.cpp host/tests/test_transport_server.cpp
git commit -m "feat(host): dispatch Scroll/MouseButton to the injector"
```

---

### Task 4: Kotlin protocol — encodeScroll + encodeMouseButton

**Files:**
- Modify: `android/app/src/main/java/com/droppix/app/protocol/Protocol.kt`
- Test: `android/app/src/test/java/com/droppix/app/protocol/ProtocolTest.kt`

**Interfaces:**
- Produces: `MsgType.SCROLL(12)`, `MsgType.MOUSE_BUTTON(13)`; `Protocol.encodeScroll(dx: Int, dy: Int, x: Int, y: Int): ByteArray`; `Protocol.encodeMouseButton(button: Int, action: Int, x: Int, y: Int): ByteArray`.

- [ ] **Step 1: Write failing tests** — add to `ProtocolTest.kt`

```kotlin
@Test fun scrollLayout() {
    val b = Protocol.encodeScroll(-3, 5, 1000, 2000)
    fun i16(o: Int) = ((b[o].toInt() shl 8) or (b[o+1].toInt() and 0xFF)).toShort().toInt()
    fun u16(o: Int) = ((b[o].toInt() and 0xFF) shl 8) or (b[o+1].toInt() and 0xFF)
    assertEquals(-3, i16(0)); assertEquals(5, i16(2)); assertEquals(1000, u16(4)); assertEquals(2000, u16(6))
}
@Test fun mouseButtonLayout() {
    val b = Protocol.encodeMouseButton(2, 1, 1234, 5678)
    assertEquals(2, b[0].toInt() and 0xFF); assertEquals(1, b[1].toInt() and 0xFF)
    fun u16(o: Int) = ((b[o].toInt() and 0xFF) shl 8) or (b[o+1].toInt() and 0xFF)
    assertEquals(1234, u16(2)); assertEquals(5678, u16(4))
}
```

- [ ] **Step 2: Run to verify FAIL**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest --tests "*ProtocolTest*"'`
Expected: FAIL.

- [ ] **Step 3: Implement.** Add `SCROLL(12), MOUSE_BUTTON(13)` to the `MsgType` enum. Add (mirroring `putU32`/the touch encoder's `ushr 8` byte writes):

```kotlin
fun encodeScroll(dx: Int, dy: Int, x: Int, y: Int): ByteArray {
    val out = ArrayList<Byte>(8)
    fun u16(v: Int) { out.add((v ushr 8).toByte()); out.add(v.toByte()) }
    u16(dx); u16(dy); u16(x); u16(y)
    return out.toByteArray()
}
fun encodeMouseButton(button: Int, action: Int, x: Int, y: Int): ByteArray {
    val out = ArrayList<Byte>(6)
    out.add(button.toByte()); out.add(action.toByte())
    out.add((x ushr 8).toByte()); out.add(x.toByte())
    out.add((y ushr 8).toByte()); out.add(y.toByte())
    return out.toByteArray()
}
```
(`dx`/`dy` may be negative; `.toByte()` truncation of the low byte + the high byte reproduces the signed 16-bit big-endian value, which the C++ `int16_t` cast reads back — the test confirms.)

- [ ] **Step 4: Run to verify PASS** — same as Step 2. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/protocol/Protocol.kt android/app/src/test/java/com/droppix/app/protocol/ProtocolTest.kt
git commit -m "feat(android/protocol): encodeScroll + encodeMouseButton"
```

---

### Task 5: Android — capture mouse scroll + right/middle buttons and send

**Files:**
- Modify: `android/app/src/main/java/com/droppix/app/net/TransportClient.kt` (send methods)
- Modify: `android/app/src/main/java/com/droppix/app/ui/GlDisplayView.kt` (capture + listeners)
- Modify: `android/app/src/main/java/com/droppix/app/ui/StreamActivity.kt` (wire listeners → client)

**Interfaces:**
- Consumes: `Protocol.encodeScroll`/`encodeMouseButton` (Task 4).
- Produces: `TransportClient.sendScroll(dx,dy,x,y)`, `TransportClient.sendMouseButton(button,action,x,y)`; `GlDisplayView` gains a `MouseListener` (`onScroll(dx,dy,x,y)`, `onMouseButton(button,action,x,y)`) + `setMouseListener(...)`.

- [ ] **Step 1: Read the send + touch-capture patterns.** In `TransportClient.kt`, read `sendOrientation`/`sendTouch` (how they encode + write a framed message to the output stream, thread-safely). In `GlDisplayView.kt`, read the `TouchListener` interface + `setTouchListener` + how `onTouchEvent` normalizes a pointer to 0..65535 and emits contacts.

- [ ] **Step 2: `TransportClient` send methods** (mirror `sendOrientation` exactly — same lock/output write):

```kotlin
fun sendScroll(dx: Int, dy: Int, x: Int, y: Int) = send(MsgType.SCROLL, Protocol.encodeScroll(dx, dy, x, y))
fun sendMouseButton(button: Int, action: Int, x: Int, y: Int) = send(MsgType.MOUSE_BUTTON, Protocol.encodeMouseButton(button, action, x, y))
```
(Use whatever the existing private send/write helper is that `sendOrientation` uses; if `sendOrientation` inlines the write, inline the same way.)

- [ ] **Step 3: `GlDisplayView` capture.** Add a `MouseListener` interface (`onScroll(dx:Int,dy:Int,x:Int,y:Int)`, `onMouseButton(button:Int,action:Int,x:Int,y:Int)`) + `@Volatile private var mouseListener` + `setMouseListener`. Override:

```kotlin
override fun onGenericMotionEvent(event: MotionEvent): Boolean {
    if (event.source and InputDevice.SOURCE_MOUSE != 0 && event.action == MotionEvent.ACTION_SCROLL) {
        val v = Math.round(event.getAxisValue(MotionEvent.AXIS_VSCROLL))
        val h = Math.round(event.getAxisValue(MotionEvent.AXIS_HSCROLL))
        if (v != 0 || h != 0) mouseListener?.onScroll(h, v, normX(event.x), normY(event.y))
        return true
    }
    return super.onGenericMotionEvent(event)
}
```
And in `onTouchEvent`, when `event.source and InputDevice.SOURCE_MOUSE != 0`, detect right/middle button press/release via `event.actionButton`/`buttonState` on `ACTION_BUTTON_PRESS`/`ACTION_BUTTON_RELEASE`:

```kotlin
if (event.source and InputDevice.SOURCE_MOUSE != 0 &&
    (event.actionMasked == MotionEvent.ACTION_BUTTON_PRESS || event.actionMasked == MotionEvent.ACTION_BUTTON_RELEASE)) {
    val down = event.actionMasked == MotionEvent.ACTION_BUTTON_PRESS
    val btn = when (event.actionButton) {
        MotionEvent.BUTTON_SECONDARY -> 1   // right
        MotionEvent.BUTTON_TERTIARY -> 2    // middle
        else -> 0
    }
    if (btn != 0) { mouseListener?.onMouseButton(btn, if (down) 1 else 0, normX(event.x), normY(event.y)); return true }
}
```
Use the SAME x/y → 0..65535 normalization the touch path uses (`normX`/`normY` = whatever helper it already applies — reuse it; do not invent a different scale). Left-button and finger touches fall through to the existing touch handling unchanged.

- [ ] **Step 4: Wire in `StreamActivity`.** Where the `TouchListener` is set on the surface (in `onResume`/setup), also `surface.setMouseListener(object : GlDisplayView.MouseListener { override fun onScroll(...) { client?.sendScroll(...) }; override fun onMouseButton(...) { client?.sendMouseButton(...) } })`.

- [ ] **Step 5: Build + unit tests**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew assembleDebug testDebugUnitTest'`
Expected: BUILD SUCCESSFUL; unit tests green.

- [ ] **Step 6: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/net/TransportClient.kt android/app/src/main/java/com/droppix/app/ui/GlDisplayView.kt android/app/src/main/java/com/droppix/app/ui/StreamActivity.kt
git commit -m "feat(android): send mouse scroll + right/middle buttons"
```

---

### Task 6: Linux client — capture wheel + right/middle and send

**Files:**
- Modify: `client/gui/video_widget.h`, `client/gui/video_widget.cpp` (wheel/button capture + callbacks; remove the right-click two-finger synth)
- Modify: `client/src/transport_client.h`, `client/src/transport_client.cpp` (send methods)
- Modify: `client/gui/main_window.cpp` (wire widget callbacks → transport)

**Interfaces:**
- Consumes: `encode_scroll`/`encode_mouse_button` (Task 1).
- Produces: `VideoWidget` gains `setScrollCallback(std::function<void(int,int,uint16_t,uint16_t)>)` and `setMouseButtonCallback(std::function<void(uint8_t,uint8_t,uint16_t,uint16_t)>)`; `TransportClient` gains `sendScroll(...)`/`sendMouseButton(...)`.

- [ ] **Step 1: Read the widget + send patterns.** In `video_widget.{h,cpp}`, read the `TouchCallback` mechanism, the `normalize(...)` helper (pixel → 0..65535), and the current right-click handling (the code that synthesizes a two-contact tap on `Qt::RightButton`). In `transport_client.{h,cpp}` and `main_window.cpp`, read how a `TouchCallback` is forwarded to the transport (the send path that frames + writes a message).

- [ ] **Step 2: `VideoWidget` capture.**
  - Add the two callback setters + members.
  - `void VideoWidget::wheelEvent(QWheelEvent* e)`: `QPoint d = e->angleDelta(); int dx = d.x()/120; int dy = d.y()/120;` then `if (scrollCb_ && (dx||dy)) { auto n = normalize(e->position().x(), e->position().y()); scrollCb_(dx, dy, n.x, n.y); } e->accept();`
  - In `mousePressEvent`/`mouseReleaseEvent`: on `Qt::RightButton`/`Qt::MiddleButton`, call `mouseButtonCb_(btn, down?1:0, nx, ny)` where `btn` = 1 for RightButton, 2 for MiddleButton — and **remove** the existing right-click→two-finger-tap synthesis (the direct button replaces it). Left button still flows through the existing touch-contact synthesis.
  (Use the file's existing `normalize(...)` for the pointer position — same scale as touch.)

- [ ] **Step 3: `TransportClient` send methods** (mirror the existing send-touch/orientation path):

```cpp
void TransportClient::sendScroll(int dx, int dy, uint16_t x, uint16_t y) { send(MsgType::Scroll, encode_scroll((int16_t)dx, (int16_t)dy, x, y)); }
void TransportClient::sendMouseButton(uint8_t button, uint8_t action, uint16_t x, uint16_t y) { send(MsgType::MouseButton, encode_mouse_button(button, action, x, y)); }
```
(Use whatever framed-send helper the touch path uses; match its threading/locking.)

- [ ] **Step 4: Wire in `main_window.cpp`.** Where the widget's `TouchCallback` is connected to the transport, also connect the two new callbacks to `transport->sendScroll(...)` / `transport->sendMouseButton(...)`.

- [ ] **Step 5: Build + client suite**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build --output-on-failure'`
Expected: clean build; client suite green.

- [ ] **Step 6: Commit**

```bash
git add client/gui/video_widget.h client/gui/video_widget.cpp client/src/transport_client.h client/src/transport_client.cpp client/gui/main_window.cpp
git commit -m "feat(client): send mouse wheel + right/middle buttons (direct, replacing two-finger right-click)"
```

---

### Task 7: Verification

**Files:** none.

- [ ] **Step 1: Full builds + suites**

```
distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build --output-on-failure && cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build --output-on-failure'
distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest assembleDebug'
```
Expected: host + client suites green (incl. `Protocol.ScrollRoundTrip`, `Protocol.MouseButtonRoundTrip`, `TransportServer.ScrollHandlerFires`); Android unit tests green; APK builds.

- [ ] **Step 2: On-device (user's tablet + a mouse; evdi + Touch enabled on the host).** With a physical mouse on the tablet (or the Linux client): scroll wheel scrolls the window under the pointer; right-button = right-click; middle-button = middle-click; two-finger-tap still right-clicks (finger). Left-click/drag + cursor movement unaffected.

- [ ] **Step 3: Commit any fixes; otherwise done.**

---

## Self-review notes

- **Spec coverage:** Scroll/MouseButton protocol (T1 C++, T4 Kotlin); host aux-device generalization (T2) + dispatch/wiring (T3); Android capture+send (T5); Linux capture+send incl. removing the two-finger right-click synth (T6); testing per-task + T7. Two-finger gesture kept (untouched — only the Linux client's *synth* is removed; the host gesture stays).
- **No HELLO/version change** — Scroll/MouseButton are new independent `MsgType`s; no task touches HELLO/kProtocolVersion.
- **Type consistency:** `encode_scroll(int16_t dx,int16_t dy,uint16_t x,uint16_t y)` / `encode_mouse_button(uint8_t,uint8_t,uint16_t,uint16_t)` and their Kotlin `encodeScroll(Int,Int,Int,Int)`/`encodeMouseButton(Int,Int,Int,Int)`; handler sigs, `injector.scroll`/`mouse_button`, client `sendScroll`/`sendMouseButton` all consistent across tasks. Button codes 1=right/2=middle and action 0=up/1=down used everywhere.
- **Gate:** T3 wires the handlers inside the same evdi+`cfg_.touch` block as touch, and nulls them at session start — so no stale-injector call.
