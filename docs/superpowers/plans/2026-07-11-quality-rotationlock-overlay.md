# Quality / Rotation-lock / Overlay Settings — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add three Spacedesk-parity client settings — client-chosen **Quality/bitrate** (host + protocol + both clients), Android **Rotation lock**, and an Android **Performance-overlay** toggle.

**Architecture:** Quality reuses the client-owned-settings pattern: a new `bitrate_kbps` field in the HELLO handshake (bumped to **v5**), which the host prefers over its CLI default via the pure `select_session_params` helper. Rotation-lock and the overlay toggle are Android-only view/lifecycle changes with no protocol impact. Back-compat is preserved by version-gating: v4/v3/v2 clients decode `bitrate=0` and the host falls back to its default.

**Tech Stack:** C++17 (host + Linux client, CMake), Kotlin/Android (Gradle), Qt6 (Linux client). Design: `docs/superpowers/specs/2026-07-11-quality-rotationlock-overlay-design.md`.

## Global Constraints

- **HELLO v5 wire body** (all integers big-endian), the authoritative layout every encoder/decoder must match:
  `u32 version, u32 width, u32 height, u32 density, u32 fps, u8 audio_wanted, u8 orientation_code, u32 bitrate_kbps, u16-len name, u16-len id`.
  The `u32 bitrate_kbps` is written **only when `version >= 5`**, immediately after `orientation_code`, before the strings. The v4 fields (`fps`/`audio_wanted`/`orientation_code`) are still written only when `version >= 4`. A v4 body is unchanged (strings at offset 22); a v5 body has strings at offset 26.
- `kProtocolVersion` (C++) and `Protocol.VERSION` (Kotlin) become **5**.
- **Quality presets:** Low / Medium / High = **4000 / 8000 / 16000** kbps. 8000 is today's host default; it is also the default for every new `bitrate` settings field.
- **Back-compat:** version-gate every new field; a client sending v<5 makes the host use its `cfg_.bitrate_kbps` default. No host change is required for a v4 client to keep working.
- **Rotation-lock / overlay are Android-only** — no host or protocol change.
- **The orientation restart-loop invariant still holds:** Android always sends `orientationMapper.currentCode()` in HELLO (both rotation modes).
- **Build/test environments** (all verified this session; the repo is on a CIFS no-exec mount):
  - Host: `distrobox enter droppix-dev -- bash -lc 'cmake -S host -B ~/droppix-build && cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build <-R filter> --output-on-failure'`
  - Linux client: `distrobox enter droppix-dev -- bash -lc 'cmake -S client -B ~/droppix-client-build -DDROPPIX_CLIENT_BUILD_TESTS=ON && cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build <-R filter> --output-on-failure'`
  - Android: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew <task>'` — use `sh gradlew` (NOT `./gradlew`; CIFS no-exec) and always set `ANDROID_HOME`.
- Work on the current branch `feat/client-settings-quality` (stacked on `feat/android-client-settings`). Commit after each task.

---

### Task 1: C++ HELLO v5 (bitrate in protocol)

**Files:**
- Modify: `host/src/protocol.h` (bump `kProtocolVersion`; extend `encode_hello`/`decode_hello` decls)
- Modify: `host/src/protocol.cpp` (write/read `bitrate_kbps`, version-gated)
- Test: `host/tests/test_protocol.cpp`

**Interfaces:**
- Produces:
  - `std::vector<unsigned char> encode_hello(uint32_t version, uint32_t width, uint32_t height, uint32_t density, const std::string& name, const std::string& id, uint32_t fps = 0, uint8_t audio_wanted = 0, uint8_t orientation_code = 0, uint32_t bitrate_kbps = 0);`
  - `bool decode_hello(const std::vector<unsigned char>& body, uint32_t& version, uint32_t& width, uint32_t& height, uint32_t& density, uint32_t& fps, uint8_t& audio_wanted, uint8_t& orientation_code, uint32_t& bitrate_kbps, std::string& name, std::string& id);` (the existing 7-arg legacy overload stays untouched).
  - `constexpr uint32_t kProtocolVersion = 5;`

- [ ] **Step 1: Read the current v4 code as the template**

Read `host/src/protocol.cpp` `encode_hello`/`decode_hello` and `host/src/protocol.h`. The v4 addition (a `if (version >= 4) { put_u32(fps); push audio; push orient; }` in encode, and a `version >= 4` branch reading fps@16/audio@20/orient@21/strings@22 in decode) is the exact pattern to mirror for v5. Use the same `put_u32`/`get_u32`/string helpers already in the file.

- [ ] **Step 2: Write the failing tests** — add to `host/tests/test_protocol.cpp`

```cpp
TEST(Protocol, HelloV5CarriesBitrate) {
  auto body = droppix::encode_hello(5, 1280, 720, 160, "n", "i",
                                    /*fps*/30, /*audio*/1, /*orient*/1, /*bitrate*/12000);
  uint32_t ver, w, h, d, fps, br; uint8_t audio, ori; std::string name, id;
  ASSERT_TRUE(droppix::decode_hello(body, ver, w, h, d, fps, audio, ori, br, name, id));
  EXPECT_EQ(ver, 5u); EXPECT_EQ(fps, 30u); EXPECT_EQ(audio, 1); EXPECT_EQ(ori, 1);
  EXPECT_EQ(br, 12000u); EXPECT_EQ(name, "n"); EXPECT_EQ(id, "i");
}

TEST(Protocol, HelloV4DecodesBitrateSentinelZero) {
  auto body = droppix::encode_hello(4, 1280, 720, 160, "n", "i", 30, 1, 1 /*no bitrate*/);
  uint32_t ver, w, h, d, fps, br; uint8_t audio, ori; std::string name, id;
  ASSERT_TRUE(droppix::decode_hello(body, ver, w, h, d, fps, audio, ori, br, name, id));
  EXPECT_EQ(ver, 4u); EXPECT_EQ(br, 0u);         // no bitrate field on a v4 body
  EXPECT_EQ(name, "n"); EXPECT_EQ(id, "i");      // strings still parse (offset 22)
}
```

- [ ] **Step 3: Run to verify FAIL**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S host -B ~/droppix-build && cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build -R Protocol --output-on-failure'`
Expected: compile error / FAIL — `decode_hello` has no 11-arg (bitrate) overload.

- [ ] **Step 4: Implement**

In `host/src/protocol.h`: set `constexpr uint32_t kProtocolVersion = 5;`. Add `uint32_t bitrate_kbps = 0` as the trailing param of `encode_hello`, and add the 11-arg `decode_hello` declaration (with `uint32_t& bitrate_kbps` inserted after `orientation_code`, before `name`).

In `host/src/protocol.cpp`:
- `encode_hello`: after the `if (version >= 4) {...}` block, add `if (version >= 5) { put_u32(b, bitrate_kbps); }` (before the name/id writes).
- `decode_hello` (11-arg): default `bitrate_kbps = 0`. In the `version >= 4` branch, after reading orientation (offset 21) and setting the string cursor to 22, add: `if (version >= 5) { bitrate_kbps = get_u32(body, 22); p = 26; }` (read bitrate at 22, move the string cursor to 26).
- **Overload handling:** make the 11-arg `decode_hello` canonical, and KEEP the existing 10-arg `decode_hello` as a thin forwarder that calls the 11-arg with a discarded local `uint32_t bitrate` — this way the existing v4/v3 tests in `test_protocol.cpp` (which call the 10-arg) compile unchanged, and the 7-arg legacy overload is likewise untouched. `read_hello` is migrated to the 11-arg in Task 3.

- [ ] **Step 5: Run to verify PASS**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build -R Protocol --output-on-failure'`
Expected: PASS (incl. the two new tests and all existing v2/v3/v4 protocol tests).

- [ ] **Step 6: Commit**

```bash
git add host/src/protocol.h host/src/protocol.cpp host/tests/test_protocol.cpp
git commit -m "feat(protocol): HELLO v5 carries bitrate_kbps (back-compat v4)"
```

---

### Task 2: `select_session_params` picks bitrate

**Files:**
- Modify: `host/src/session_params.h`, `host/src/session_params.cpp`
- Test: `host/tests/test_session_params.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `struct SessionParams { int fps; bool audio; int orientation; int bitrate; };`
  - `SessionParams select_session_params(uint32_t client_version, uint32_t hello_fps, uint8_t hello_audio, uint8_t hello_orientation, uint32_t hello_bitrate, int default_fps, bool default_audio, int default_orientation, int default_bitrate);`

- [ ] **Step 1: Write the failing tests** — add to `host/tests/test_session_params.cpp`

```cpp
TEST(SessionParams, V5PrefersClientBitrate) {
  auto p = select_session_params(5, 60, 1, 1, 12000, 30, false, 0, 8000);
  EXPECT_EQ(p.bitrate, 12000); EXPECT_EQ(p.fps, 60);
}
TEST(SessionParams, V4HasNoBitrateFieldUsesDefault) {
  auto p = select_session_params(4, 60, 1, 1, 12000, 30, false, 0, 8000);
  EXPECT_EQ(p.bitrate, 8000);   // bitrate is v5-only; a v4 client's hello_bitrate is meaningless -> default
}
TEST(SessionParams, V5ZeroBitrateFallsBack) {
  auto p = select_session_params(5, 60, 1, 1, 0, 30, false, 0, 8000);
  EXPECT_EQ(p.bitrate, 8000);
}
```

Also update the FOUR existing `select_session_params` tests in this file to pass the two new args (`hello_bitrate` after `hello_orientation`, `default_bitrate` at the end) — use `0, 8000` so their existing assertions are unchanged, and add `EXPECT_EQ(p.bitrate, 8000)` where natural.

- [ ] **Step 2: Run to verify FAIL**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build -R SessionParams --output-on-failure'`
Expected: FAIL — arity mismatch / no `bitrate` member.

- [ ] **Step 3: Implement**

`session_params.h`: add `int bitrate;` to the struct; extend the signature with `uint32_t hello_bitrate` (after `hello_orientation`) and `int default_bitrate` (last).

`session_params.cpp`:
```cpp
SessionParams select_session_params(uint32_t cver, uint32_t hfps, uint8_t haudio, uint8_t hori,
                                    uint32_t hbitrate, int dfps, bool daudio, int dori, int dbitrate) {
  if (cver >= 4) {
    return { hfps > 0 ? static_cast<int>(hfps) : dfps,
             haudio != 0,
             static_cast<int>(hori & 3),
             (cver >= 5 && hbitrate > 0) ? static_cast<int>(hbitrate) : dbitrate };
  }
  return { dfps, daudio, dori, dbitrate };
}
```

- [ ] **Step 4: Run to verify PASS**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build -R SessionParams --output-on-failure'`
Expected: PASS (3 new + 4 updated).

- [ ] **Step 5: Commit**

```bash
git add host/src/session_params.h host/src/session_params.cpp host/tests/test_session_params.cpp
git commit -m "feat(host): select_session_params picks client bitrate (v5) with fallback"
```

---

### Task 3: Host applies the client bitrate

**Files:**
- Modify: `host/src/transport_server.h`, `host/src/transport_server.cpp` (`read_hello` gains a `bitrate` out-param)
- Modify: `host/src/stream_daemon.cpp` (pass `sp.bitrate` to `enc_.open`; call `select_session_params` with bitrate args)
- Test: `host/tests/test_transport_server.cpp`

**Interfaces:**
- Consumes: `decode_hello` 11-arg (Task 1), `select_session_params` 9-arg (Task 2).
- Produces: `read_hello(..., uint32_t& bitrate, ...)` — one more out-param, inserted after `orientation`.

- [ ] **Step 1: Read the current `read_hello` + `stream_daemon` HELLO block**

Read `host/src/transport_server.{h,cpp}` `read_hello` and the `stream_daemon.cpp` region around the `select_session_params` call and `enc_.open` (~lines 40-77 from prior work). Note the current out-param order.

- [ ] **Step 2: Write the failing test** — extend the existing `ReadHelloV4Fields` test (or add `ReadHelloV5Bitrate`) in `host/tests/test_transport_server.cpp`

Follow the SAME channel-pairing/setup pattern the existing tests use. Send a v5 HELLO via `encode_hello(5, 1280,720,160,"n","i", 30,1,1, 9000)` through the paired channel and assert `read_hello` surfaces `bitrate == 9000` (alongside the existing fps/audio/orientation assertions). Update the two pre-existing `read_hello` call sites in this file to the new arity (add a `uint32_t bitrate;` out-arg after `orientation`).

- [ ] **Step 3: Run to verify FAIL**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build -R TransportServer --output-on-failure'`
Expected: FAIL — `read_hello` arity / missing bitrate.

- [ ] **Step 4: Implement**

- `read_hello`: add `uint32_t& bitrate` after `orientation`; pass it through the `decode_hello` 11-arg call.
- `stream_daemon.cpp`: read the new `bitrate` from `read_hello`; call `select_session_params(cver, hfps, haudio, hori, hbitrate, cfg_.fps, cfg_.audio, cfg_.orientation, cfg_.bitrate_kbps)`; change `enc_.open(w, h, sp.fps, cfg_.bitrate_kbps)` → `enc_.open(w, h, sp.fps, sp.bitrate)`.

- [ ] **Step 5: Run to verify PASS + full host suite**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build --output-on-failure'`
Expected: full host suite PASS.

- [ ] **Step 6: Commit**

```bash
git add host/src/transport_server.h host/src/transport_server.cpp host/src/stream_daemon.cpp host/tests/test_transport_server.cpp
git commit -m "feat(host): honor client-requested bitrate (v5) at the encoder"
```

---

### Task 4: Kotlin HELLO v5 (bitrate)

**Files:**
- Modify: `android/app/src/main/java/com/droppix/app/protocol/Protocol.kt`
- Test: `android/app/src/test/java/com/droppix/app/protocol/ProtocolTest.kt`

**Interfaces:**
- Produces: `fun encodeHello(version, width, height, density, name="", id="", fps=0, audioWanted=0, orientationCode=0, bitrateKbps=0): ByteArray`; `const val VERSION = 5`.

- [ ] **Step 1: Write the failing tests** — add to `ProtocolTest.kt`

```kotlin
@Test fun helloV5CarriesBitrate() {
    val b = Protocol.encodeHello(5, 1280, 720, 160, "n", "i",
                                 fps = 30, audioWanted = 1, orientationCode = 1, bitrateKbps = 12000)
    fun u32(o: Int) = ((b[o].toInt() and 0xFF) shl 24) or ((b[o+1].toInt() and 0xFF) shl 16) or
                      ((b[o+2].toInt() and 0xFF) shl 8) or (b[o+3].toInt() and 0xFF)
    assertEquals(5, u32(0)); assertEquals(30, u32(16))
    assertEquals(1, b[20].toInt() and 0xFF); assertEquals(1, b[21].toInt() and 0xFF)
    assertEquals(12000, u32(22))                                   // bitrate
    assertEquals(0, b[26].toInt() and 0xFF); assertEquals(1, b[27].toInt() and 0xFF)  // name-len @26
    assertEquals('n'.code, b[28].toInt() and 0xFF)
}
@Test fun helloV4OmitsBitrate() {
    val b = Protocol.encodeHello(4, 1280, 720, 160, "n", "i", fps = 30, audioWanted = 1, orientationCode = 1)
    assertEquals(0, b[22].toInt() and 0xFF); assertEquals(1, b[23].toInt() and 0xFF)  // name-len @22
    assertEquals('n'.code, b[24].toInt() and 0xFF)
}
```

- [ ] **Step 2: Run to verify FAIL**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest --tests "*ProtocolTest*"'`
Expected: FAIL — `encodeHello` has no `bitrateKbps`.

- [ ] **Step 3: Implement**

Set `const val VERSION = 5`. Add `bitrateKbps: Int = 0` as the trailing param; after the `if (version >= 4) {...}` block add:
```kotlin
if (version >= 5) { putU32(out, bitrateKbps) }
```

- [ ] **Step 4: Run to verify PASS**

Run: same as Step 2. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/protocol/Protocol.kt android/app/src/test/java/com/droppix/app/protocol/ProtocolTest.kt
git commit -m "feat(android/protocol): HELLO v5 carries bitrate; VERSION=5"
```

---

### Task 5: Android `AppSettings` — bitrate, rotationLocked, showOverlay

**Files:**
- Modify: `android/app/src/main/java/com/droppix/app/settings/AppSettings.kt`
- Test: `android/app/src/test/java/com/droppix/app/settings/AppSettingsTest.kt`

**Interfaces:**
- Produces: `data class AppSettings(width=0, height=0, fps=60, audio=false, bitrateKbps=8000, rotationLocked=false, showOverlay=false)`; `SettingsStore.load/save` persist the three new keys.

- [ ] **Step 1: Write the failing test** — add to `AppSettingsTest.kt`

```kotlin
@Test fun newDefaults() {
    val s = AppSettings()
    assertEquals(8000, s.bitrateKbps); assertFalse(s.rotationLocked); assertFalse(s.showOverlay)
}
```

- [ ] **Step 2: Run to verify FAIL**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest --tests "*AppSettingsTest*"'`
Expected: FAIL — no `bitrateKbps`/`rotationLocked`/`showOverlay`.

- [ ] **Step 3: Implement**

```kotlin
data class AppSettings(
    val width: Int = 0, val height: Int = 0, val fps: Int = 60, val audio: Boolean = false,
    val bitrateKbps: Int = 8000, val rotationLocked: Boolean = false, val showOverlay: Boolean = false)
```
In `SettingsStore.load()` add `bitrateKbps = prefs.getInt("bitrate", 8000)`, `rotationLocked = prefs.getBoolean("rot_lock", false)`, `showOverlay = prefs.getBoolean("overlay", false)`. In `save()` add `.putInt("bitrate", s.bitrateKbps).putBoolean("rot_lock", s.rotationLocked).putBoolean("overlay", s.showOverlay)`.

- [ ] **Step 4: Run to verify PASS**

Run: same as Step 2. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/settings/AppSettings.kt android/app/src/test/java/com/droppix/app/settings/AppSettingsTest.kt
git commit -m "feat(android): AppSettings gains bitrate, rotationLocked, showOverlay"
```

---

### Task 6: Android `TransportClient` threads bitrate

**Files:**
- Modify: `android/app/src/main/java/com/droppix/app/net/TransportClient.kt`
- Modify (call sites): `android/app/src/main/java/com/droppix/app/ui/StreamActivity.kt`, `android/app/src/test/java/com/droppix/app/net/TransportClientTest.kt`, `TransportClientStatsTest.kt`

**Interfaces:**
- Consumes: `encodeHello(... bitrateKbps)` (Task 4).
- Produces: `run(...)` and `runOverChannel(...)` gain `bitrateKbps: Int` inserted **after** `orientationCode`.

- [ ] **Step 1: Read current signatures.** Confirm the exact param order in `TransportClient.kt` (`run` ~63, `runOverChannel` ~95). Insert `bitrateKbps: Int` right after `orientationCode` in both; forward it from `run`→`runOverChannel` and from `runOverChannel`→`encodeHello`.

- [ ] **Step 2: Implement** the signature + forwarding, and pass `Protocol.encodeHello(Protocol.VERSION, width, height, density, name, id, fps, audioWanted, orientationCode, bitrateKbps)`.

- [ ] **Step 3: Fix call sites.** In `StreamActivity.kt` both call sites (AOA + Wi-Fi) add a bitrate arg after `orientationCode` — use a temporary literal `8000` here (Task 7 replaces it with the real setting). In the two test files, add `8000` after the orientation arg to each `run`/`runOverChannel` call.

- [ ] **Step 4: Build + tests**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest --tests "*TransportClient*"'`
Expected: PASS (existing transport tests green with the new arity).

- [ ] **Step 5: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/net/TransportClient.kt android/app/src/main/java/com/droppix/app/ui/StreamActivity.kt android/app/src/test/java/com/droppix/app/net/TransportClientTest.kt android/app/src/test/java/com/droppix/app/net/TransportClientStatsTest.kt
git commit -m "feat(android/net): TransportClient threads bitrate into HELLO v5"
```

---

### Task 7: Android `StreamActivity` — send bitrate + rotation lock + overlay

**Files:**
- Modify: `android/app/src/main/java/com/droppix/app/ui/StreamActivity.kt`

**Interfaces:**
- Consumes: `AppSettings.bitrateKbps/rotationLocked/showOverlay` (Task 5); `run/runOverChannel(... bitrateKbps)` (Task 6).

- [ ] **Step 1: Read** `startStreaming()` (where `settings` is loaded and `sendW/sendH/sendFps/sendAudio` are computed), the `orientationListener` callback (~74-82), the `onResume` overlay-tick post, and the `overlay` view init.

- [ ] **Step 2: Send the real bitrate.** In `startStreaming()`, add `val sendBitrate = settings.bitrateKbps` and replace the `8000` literal at BOTH call sites with `sendBitrate` (right after `orientationMapper.currentCode()`).

- [ ] **Step 3: Rotation lock.** Add a field `@Volatile private var rotationLocked = false`. In `startStreaming()` set `rotationLocked = settings.rotationLocked` and apply the activity orientation:
```kotlin
requestedOrientation = if (settings.rotationLocked)
    android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LOCKED
else
    android.content.pm.ActivityInfo.SCREEN_ORIENTATION_FULL_SENSOR
```
In the `orientationListener` callback, guard the send: `if (!rotationLocked) client?.sendOrientation(code)` (leave the `orientationMapper.update(...)` call so `currentCode()` stays correct for the HELLO seed).

- [ ] **Step 4: Overlay toggle.** In `startStreaming()`, set `overlay.visibility = if (settings.showOverlay) View.VISIBLE else View.GONE`. Leave the host `onOverlay` path intact (it may still force it visible). (The `overlayTick` already updates the text every tick; it only shows when visible.)

- [ ] **Step 5: Build + full unit suite**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew assembleDebug testDebugUnitTest'`
Expected: `BUILD SUCCESSFUL`, unit tests green.

- [ ] **Step 6: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/ui/StreamActivity.kt
git commit -m "feat(android): apply client bitrate, rotation lock, and overlay toggle"
```

---

### Task 8: Android `SettingsActivity` — Quality / Rotation / Overlay controls

**Files:**
- Modify: `android/app/src/main/java/com/droppix/app/ui/SettingsActivity.kt`
- Modify: `android/app/src/main/res/layout/activity_settings.xml`

**Interfaces:**
- Consumes: `AppSettings`/`SettingsStore` (Task 5), the existing `lightAdapter(...)` helper and `spinner_item`/`spinner_dropdown_item` layouts, `Resolutions`.

- [ ] **Step 1: Layout.** In `activity_settings.xml`, add three labelled controls (mirror the existing Resolution/Frame-rate rows exactly, including `popupBackground="#232830"` on the spinners and label `textColor="#9aa5b1"`):
  - a label "Quality" + `Spinner` `@+id/quality_spinner`
  - a label "Rotation" + `Spinner` `@+id/rotation_spinner`
  - inside the audio row's style, a "Performance overlay" label + `Switch` `@+id/overlay_switch` (copy the Audio row block).

- [ ] **Step 2: Wire in `SettingsActivity.onCreate`** (after the fps spinner block; use `lightAdapter` so text is white):
```kotlin
val qualitySpinner = findViewById<Spinner>(R.id.quality_spinner)
val qualityKbps = listOf(4000, 8000, 16000)                 // Low / Medium / High
val qualityLabels = listOf("Low", "Medium", "High")
qualitySpinner.adapter = lightAdapter(qualityLabels)
qualitySpinner.setSelection(qualityKbps.indexOf(cur.bitrateKbps).coerceAtLeast(1))  // default Medium

val rotationSpinner = findViewById<Spinner>(R.id.rotation_spinner)
rotationSpinner.adapter = lightAdapter(listOf("Auto", "Locked"))
rotationSpinner.setSelection(if (cur.rotationLocked) 1 else 0)

val overlaySwitch = findViewById<Switch>(R.id.overlay_switch)
overlaySwitch.isChecked = cur.showOverlay
```
Then extend the Save `setOnClickListener` to persist them:
```kotlin
store.save(AppSettings(
    res.first, res.second,
    fpsItems[fpsSpinner.selectedItemPosition],
    audioSwitch.isChecked,
    qualityKbps[qualitySpinner.selectedItemPosition],
    rotationSpinner.selectedItemPosition == 1,
    overlaySwitch.isChecked))
```

- [ ] **Step 3: Build**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew assembleDebug'`
Expected: `BUILD SUCCESSFUL`.

- [ ] **Step 4: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/ui/SettingsActivity.kt android/app/src/main/res/layout/activity_settings.xml
git commit -m "feat(android): Quality / Rotation / Performance-overlay controls in Settings"
```

---

### Task 9: Linux client `ClientSettings` — bitrate

**Files:**
- Modify: `client/src/client_settings.h`, `client/src/client_settings.cpp`
- Test: `client/tests/test_client_settings.cpp`

**Interfaces:**
- Produces: `ClientSettings.bitrate_kbps` (default 8000); `ClientSettingsStore::load/save` persist key `bitrate`.

- [ ] **Step 1: Write the failing test** — add to `test_client_settings.cpp`

```cpp
TEST(ClientSettings, BitrateDefaultAndRoundTrip) {
  droppix::ClientSettings s; EXPECT_EQ(s.bitrate_kbps, 8000);
  s.bitrate_kbps = 16000; droppix::ClientSettingsStore::save(s);
  EXPECT_EQ(droppix::ClientSettingsStore::load().bitrate_kbps, 16000);
}
```
(Follow the existing test's `QSettings::setDefaultFormat(IniFormat)` isolation pattern already in this file.)

- [ ] **Step 2: Run to verify FAIL**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S client -B ~/droppix-client-build -DDROPPIX_CLIENT_BUILD_TESTS=ON && cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build -R ClientSettings --output-on-failure'`
Expected: FAIL — no `bitrate_kbps`.

- [ ] **Step 3: Implement.** Add `int bitrate_kbps = 8000;` to `ClientSettings`. In `load()` add `s.bitrate_kbps = q.value("bitrate", 8000).toInt();`; in `save()` add `q.setValue("bitrate", s.bitrate_kbps);`.

- [ ] **Step 4: Run to verify PASS**

Run: same as Step 2. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add client/src/client_settings.h client/src/client_settings.cpp client/tests/test_client_settings.cpp
git commit -m "feat(client): ClientSettings gains bitrate_kbps"
```

---

### Task 10: Linux client — Quality dropdown + send bitrate

**Files:**
- Modify: `client/gui/settings_dialog.h`, `client/gui/settings_dialog.cpp` (Quality combo)
- Modify: `client/src/transport_client.h`, `client/src/transport_client.cpp` (`runOverChannel` gains bitrate → `encode_hello`)
- Modify: `client/gui/main_window.cpp` (pass `settings_.bitrate_kbps`)

**Interfaces:**
- Consumes: `ClientSettings.bitrate_kbps` (Task 9); `encode_hello(... bitrate_kbps)` (Task 1).
- Produces: `runOverChannel(..., uint32_t bitrate_kbps, ...)` inserted after `orientation`.

- [ ] **Step 1: Read** `ClientSettingsDialog` (how the Resolution/FPS/Rotation combos are built + `result()`), `transport_client.{h,cpp}` `runOverChannel` (its param order + the `encode_hello` call), and the `runOverChannel` call site in `main_window.cpp::netThreadMain`.

- [ ] **Step 2: Dialog Quality combo.** In `ClientSettingsDialog`, add a `QComboBox* bitrate_` with items Low/Medium/High mapped to 4000/8000/16000 (mirror the FPS combo's `addItem(text, QVariant(value))` + `findData` seeding). Seed from `s.bitrate_kbps`. In `result()`, set `out.bitrate_kbps = bitrate_->currentData().toInt();`.

- [ ] **Step 3: Transport.** In `transport_client.{h,cpp}`, add `uint32_t bitrate_kbps` to `runOverChannel` after `orientation_code`; pass it into `encode_hello(kProtocolVersion, width, height, density, name, id, fps, audio_wanted, orientation_code, bitrate_kbps)`.

- [ ] **Step 4: Wire the call site.** In `main_window.cpp::netThreadMain`, pass `static_cast<uint32_t>(settings_.bitrate_kbps)` to `runOverChannel` (after the orientation arg).

- [ ] **Step 5: Build + client suite**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build --output-on-failure'`
Expected: `BUILD` clean, client suite PASS.

- [ ] **Step 6: Commit**

```bash
git add client/gui/settings_dialog.h client/gui/settings_dialog.cpp client/src/transport_client.h client/src/transport_client.cpp client/gui/main_window.cpp
git commit -m "feat(client): Quality dropdown sends bitrate in HELLO v5"
```

---

### Task 11: Verification

**Files:** none.

- [ ] **Step 1: Full suites + APK**

```
distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build --output-on-failure && cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build --output-on-failure'
distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest assembleDebug'
```
Expected: host + client + Android suites all green; APK builds.

- [ ] **Step 2: Localhost bitrate honor-check** (reuse the harness pattern from the Phase-1 E2E — a plaintext test-pattern streamer + a HELLO sender). Send a **v5** HELLO with `bitrate=16000` and confirm the streamer's stderr reports the encoder opened at 16000 kbps (the `SoftwareEncoder::open` log line prints `bitrate_kbps`). Send a **v4** HELLO and confirm the streamer falls back to its `--bitrate` default (no crash).

- [ ] **Step 3: On-device (user's tablet)** — install the APK; in Settings set Quality = Low then High and confirm the stream's sharpness/bandwidth changes; set Rotation = Locked and confirm the display stops following the tablet; toggle Performance overlay and confirm the HUD shows/hides.

- [ ] **Step 4: Commit any fixes; otherwise done.**

---

## Self-review notes

- **Spec coverage:** Quality/bitrate → protocol v5 (T1,T4), host select+apply (T2,T3), Android (T5,T6,T7,T8), Linux client (T9,T10); Rotation lock → T7 (+control T8); Performance overlay → T7 (+control T8); testing → per-task + T11.
- **Wire layout:** v5 offsets (bitrate@22, strings@26) are asserted in both C++ (T1) and Kotlin (T4) tests, and the v4 body is asserted unchanged (strings@22).
- **Version gates:** bitrate is `>= 5` everywhere (encode, decode, select_session_params); fps/audio/orientation stay `>= 4`. A v4 client → host default bitrate (T2 test `V4HasNoBitrateFieldUsesDefault`, T1 test `HelloV4DecodesBitrateSentinelZero`).
- **Type consistency:** `bitrateKbps` (Kotlin), `bitrate_kbps` (C++), `SessionParams.bitrate` used consistently; `run`/`runOverChannel`/`encode_hello`/`encodeHello` all take the new trailing/after-orientation arg in the stated position.
- **Orientation invariant:** T7 keeps `orientationMapper.update(...)` running when locked (only the *send* is suppressed), so `currentCode()` in HELLO stays accurate — no restart loop.
```
