# Client-owned display settings — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move resolution/FPS/audio/rotation off the host GUI onto the clients, communicated via an extended HELLO handshake (v4); this plan covers the protocol, the host, and the Linux `droppix_client` (the Android app is a follow-up plan).

**Architecture:** HELLO gains `fps`, `audio_wanted`, `orientation_code` fields (v4). The host streamer prefers the client's HELLO values over its CLI-provided defaults via a pure `select_session_params` helper; audio is arbitrated across independent streamer processes with an advisory lockfile. The host GUI drops the four controls (keeping the `Settings` fields as pre-v4 fallbacks). The Linux client gains a persisted settings store + dialog and sends its choices in HELLO, reconnecting to apply changes live.

**Tech Stack:** C++17, Qt6 (Widgets), OpenSSL, GoogleTest/ctest. Builds run in the `droppix-dev` distrobox, off the CIFS mount (host → `~/droppix-build`, client → `~/droppix-client-build`).

## Global Constraints

- C++17; match surrounding code style (comment density, naming, `droppix` namespace).
- Build/test inside the `droppix-dev` distrobox, off-mount. Host: `cmake -S host -B ~/droppix-build -DCMAKE_BUILD_TYPE=Release && cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build`. Client: `cmake -S client -B ~/droppix-client-build -DCMAKE_BUILD_TYPE=Release -DDROPPIX_CLIENT_BUILD_TESTS=ON && cmake --build ~/droppix-client-build -j && ctest --test-dir ~/droppix-client-build`.
- `protocol.cpp`/`protocol.h` are shared: compiled into BOTH the host `droppix_core` and the client `droppix_client_core`. A change there must keep BOTH targets building.
- HELLO v4 wire body (all integers big-endian): `u32 version, u32 width, u32 height, u32 density, u32 fps, u8 audio_wanted, u8 orientation_code, u16-len name, u16-len id`. The three new fixed fields sit after `density`, before the strings.
- Back-compat: a `version < 4` HELLO body keeps the v2/v3 layout (strings right after `density`); the new fields decode to sentinels `fps=0, audio_wanted=0, orientation_code=0`.
- Orientation is a code `0..3` = `0/90/180/270°` (same convention as the `ORIENTATION` message and `StreamConfig::orientation`).
- Work on branch `feat/client-owned-display-settings` (already created). Commit after each task.

---

### Task 1: HELLO v4 protocol (encode/decode)

**Files:**
- Modify: `host/src/protocol.h` (bump version; extend `encode_hello`; add a 10-arg `decode_hello` overload)
- Modify: `host/src/protocol.cpp:64-84` (`encode_hello`/`decode_hello`)
- Test: `host/tests/test_protocol.cpp`

**Interfaces:**
- Produces:
  - `constexpr uint32_t kProtocolVersion = 4;`
  - `std::vector<unsigned char> encode_hello(uint32_t version, uint32_t width, uint32_t height, uint32_t density, const std::string& name, const std::string& id, uint32_t fps = 0, uint8_t audio_wanted = 0, uint8_t orientation_code = 0);` — new fields are trailing C++ params with defaults (existing call sites keep compiling) but are written in wire order (after `density`).
  - `bool decode_hello(const std::vector<unsigned char>& body, uint32_t& version, uint32_t& width, uint32_t& height, uint32_t& density, uint32_t& fps, uint8_t& audio_wanted, uint8_t& orientation_code, std::string& name, std::string& id);` — NEW 10-arg overload.
  - The existing 7-arg `decode_hello(body, version, w, h, d, name, id)` is kept, delegating to the new overload and discarding the three extra outs, so current callers keep compiling.

- [ ] **Step 1: Write the failing tests**

Add to `host/tests/test_protocol.cpp`:

```cpp
TEST(Protocol, HelloV4RoundTrip) {
  auto body = droppix::encode_hello(4, 1280, 720, 160, "tab", "id-1",
                                    /*fps=*/30, /*audio=*/1, /*orient=*/1);
  uint32_t ver, w, h, d, fps; uint8_t audio, orient; std::string name, id;
  ASSERT_TRUE(droppix::decode_hello(body, ver, w, h, d, fps, audio, orient, name, id));
  EXPECT_EQ(ver, 4u); EXPECT_EQ(w, 1280u); EXPECT_EQ(h, 720u); EXPECT_EQ(d, 160u);
  EXPECT_EQ(fps, 30u); EXPECT_EQ(audio, 1); EXPECT_EQ(orient, 1);
  EXPECT_EQ(name, "tab"); EXPECT_EQ(id, "id-1");
}

TEST(Protocol, HelloV3BackCompatDecodesSentinels) {
  // A v3 body: no fps/audio/orientation, strings right after density.
  std::vector<unsigned char> b;
  auto u32 = [&](uint32_t x){ b.push_back(x>>24); b.push_back(x>>16); b.push_back(x>>8); b.push_back(x); };
  auto u16 = [&](uint16_t x){ b.push_back(x>>8); b.push_back(x); };
  u32(3); u32(1920); u32(1080); u32(96);
  std::string name="old", id="oid"; u16(name.size()); b.insert(b.end(), name.begin(), name.end());
  u16(id.size()); b.insert(b.end(), id.begin(), id.end());
  uint32_t ver, w, h, d, fps; uint8_t audio, orient; std::string n2, i2;
  ASSERT_TRUE(droppix::decode_hello(b, ver, w, h, d, fps, audio, orient, n2, i2));
  EXPECT_EQ(ver, 3u); EXPECT_EQ(w, 1920u); EXPECT_EQ(h, 1080u);
  EXPECT_EQ(fps, 0u); EXPECT_EQ(audio, 0); EXPECT_EQ(orient, 0);   // sentinels
  EXPECT_EQ(n2, "old"); EXPECT_EQ(i2, "oid");
}

TEST(Protocol, HelloSevenArgOverloadStillWorks) {
  auto body = droppix::encode_hello(4, 800, 600, 96, "a", "b");   // trailing defaults
  uint32_t ver, w, h, d; std::string name, id;
  ASSERT_TRUE(droppix::decode_hello(body, ver, w, h, d, name, id));  // 7-arg overload
  EXPECT_EQ(w, 800u); EXPECT_EQ(name, "a"); EXPECT_EQ(id, "b");
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j 2>&1 | tail'`
Expected: FAIL — `decode_hello` has no 10-arg overload / too many args to `encode_hello`.

- [ ] **Step 3: Implement in `protocol.h`**

Bump the version and replace the two hello declarations:

```cpp
constexpr uint32_t kProtocolVersion = 4;

std::vector<unsigned char> encode_hello(uint32_t version, uint32_t width,
                                        uint32_t height, uint32_t density,
                                        const std::string& name, const std::string& id,
                                        uint32_t fps = 0, uint8_t audio_wanted = 0,
                                        uint8_t orientation_code = 0);
// Full v4 decode. Back-compatible with v3/v2 bodies (fps/audio/orientation come back 0).
bool decode_hello(const std::vector<unsigned char>& body, uint32_t& version,
                  uint32_t& width, uint32_t& height, uint32_t& density,
                  uint32_t& fps, uint8_t& audio_wanted, uint8_t& orientation_code,
                  std::string& name, std::string& id);
// Back-compat overload for callers that don't need the new fields.
bool decode_hello(const std::vector<unsigned char>& body, uint32_t& version,
                  uint32_t& width, uint32_t& height, uint32_t& density,
                  std::string& name, std::string& id);
```

- [ ] **Step 4: Implement in `protocol.cpp`** (replace lines 64-84)

```cpp
std::vector<unsigned char> encode_hello(uint32_t version, uint32_t w, uint32_t h,
                                        uint32_t d, const std::string& name, const std::string& id,
                                        uint32_t fps, uint8_t audio_wanted, uint8_t orientation_code) {
  std::vector<unsigned char> b;
  put_u32(b, version); put_u32(b, w); put_u32(b, h); put_u32(b, d);
  put_u32(b, fps); b.push_back(audio_wanted); b.push_back(orientation_code);
  put_u16(b, (uint16_t)name.size()); b.insert(b.end(), name.begin(), name.end());
  put_u16(b, (uint16_t)id.size());   b.insert(b.end(), id.begin(),   id.end());
  return b;
}
bool decode_hello(const std::vector<unsigned char>& b, uint32_t& version,
                  uint32_t& w, uint32_t& h, uint32_t& d, uint32_t& fps,
                  uint8_t& audio_wanted, uint8_t& orientation_code,
                  std::string& name, std::string& id) {
  if (b.size() < 16) return false;
  version = get_u32(b.data()); w = get_u32(b.data()+4);
  h = get_u32(b.data()+8); d = get_u32(b.data()+12);
  fps = 0; audio_wanted = 0; orientation_code = 0;
  name.clear(); id.clear();
  size_t p = 16;
  if (version >= 4) {
    if (b.size() < 22) return true;              // truncated v4 fixed block: keep sentinels
    fps = get_u32(b.data()+16); audio_wanted = b[20]; orientation_code = b[21];
    p = 22;
  }
  if (b.size() >= p+2) { uint16_t n = get_u16(b.data()+p); p += 2;
    if (b.size() >= p+n) { name.assign(b.begin()+p, b.begin()+p+n); p += n; } else return true; }
  if (b.size() >= p+2) { uint16_t n = get_u16(b.data()+p); p += 2;
    if (b.size() >= p+n) { id.assign(b.begin()+p, b.begin()+p+n); } }
  return true;
}
bool decode_hello(const std::vector<unsigned char>& b, uint32_t& version,
                  uint32_t& w, uint32_t& h, uint32_t& d, std::string& name, std::string& id) {
  uint32_t fps; uint8_t audio, orient;
  return decode_hello(b, version, w, h, d, fps, audio, orient, name, id);
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build -R Protocol --output-on-failure'`
Expected: PASS (all Protocol tests, including the three new ones). The client target is unaffected (still uses the 7-arg encode via defaults).

- [ ] **Step 6: Commit**

```bash
git add host/src/protocol.h host/src/protocol.cpp host/tests/test_protocol.cpp
git commit -m "feat(protocol): HELLO v4 carries fps/audio/orientation (back-compat v3)"
```

---

### Task 2: Host — pure `select_session_params` helper

**Files:**
- Create: `host/src/session_params.h`, `host/src/session_params.cpp`
- Modify: `host/CMakeLists.txt` (add `src/session_params.cpp` to the `droppix_core` library sources; add `tests/test_session_params.cpp` to `droppix_tests`)
- Test: `host/tests/test_session_params.cpp`

**Interfaces:**
- Produces:
  - `struct SessionParams { int fps; bool audio; int orientation; };`
  - `SessionParams select_session_params(uint32_t client_version, uint32_t hello_fps, uint8_t hello_audio, uint8_t hello_orientation, int default_fps, bool default_audio, int default_orientation);`
  - Rules: if `client_version >= 4`: `fps = hello_fps > 0 ? hello_fps : default_fps`; `audio = hello_audio != 0`; `orientation = hello_orientation & 3`. Else all three come from the defaults.

- [ ] **Step 1: Write the failing test** — `host/tests/test_session_params.cpp`

```cpp
#include <gtest/gtest.h>
#include "session_params.h"
using namespace droppix;

TEST(SessionParams, V4PrefersClientValues) {
  auto p = select_session_params(4, /*fps*/60, /*audio*/1, /*orient*/2,
                                 /*def_fps*/30, /*def_audio*/false, /*def_orient*/0);
  EXPECT_EQ(p.fps, 60); EXPECT_TRUE(p.audio); EXPECT_EQ(p.orientation, 2);
}
TEST(SessionParams, V4ZeroFpsFallsBackToDefault) {
  auto p = select_session_params(4, 0, 0, 0, 30, true, 1);
  EXPECT_EQ(p.fps, 30);            // fps sentinel 0 -> default
  EXPECT_FALSE(p.audio);           // v4 audio flag is authoritative (client didn't ask)
  EXPECT_EQ(p.orientation, 0);
}
TEST(SessionParams, PreV4UsesDefaults) {
  auto p = select_session_params(3, 60, 1, 2, 24, true, 3);
  EXPECT_EQ(p.fps, 24); EXPECT_TRUE(p.audio); EXPECT_EQ(p.orientation, 3);
}
TEST(SessionParams, OrientationMaskedToTwoBits) {
  auto p = select_session_params(4, 30, 0, 7, 30, false, 0);
  EXPECT_EQ(p.orientation, 3);     // 7 & 3
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S host -B ~/droppix-build && cmake --build ~/droppix-build -j 2>&1 | tail'`
Expected: FAIL — `session_params.h` not found.

- [ ] **Step 3: Implement `session_params.h`**

```cpp
#pragma once
#include <cstdint>
namespace droppix {
// The effective per-session display params after reconciling the client's HELLO with the
// host's fallback defaults. A v4 client is authoritative; older clients use the defaults.
struct SessionParams { int fps; bool audio; int orientation; };
SessionParams select_session_params(uint32_t client_version, uint32_t hello_fps,
                                    uint8_t hello_audio, uint8_t hello_orientation,
                                    int default_fps, bool default_audio, int default_orientation);
}  // namespace droppix
```

- [ ] **Step 4: Implement `session_params.cpp`**

```cpp
#include "session_params.h"
namespace droppix {
SessionParams select_session_params(uint32_t client_version, uint32_t hello_fps,
                                    uint8_t hello_audio, uint8_t hello_orientation,
                                    int default_fps, bool default_audio, int default_orientation) {
  if (client_version >= 4) {
    return { hello_fps > 0 ? static_cast<int>(hello_fps) : default_fps,
             hello_audio != 0,
             static_cast<int>(hello_orientation & 3) };
  }
  return { default_fps, default_audio, default_orientation };
}
}  // namespace droppix
```

Add `src/session_params.cpp` to the `droppix_core` library sources in `host/CMakeLists.txt`, and `tests/test_session_params.cpp` to the `droppix_tests` executable source list (near `tests/test_protocol.cpp`).

- [ ] **Step 5: Run test to verify it passes**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S host -B ~/droppix-build && cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build -R SessionParams --output-on-failure'`
Expected: PASS (4 tests).

- [ ] **Step 6: Commit**

```bash
git add host/src/session_params.h host/src/session_params.cpp host/tests/test_session_params.cpp host/CMakeLists.txt
git commit -m "feat(host): pure select_session_params (HELLO vs fallback)"
```

---

### Task 3: Host — read HELLO fields + wire into the streamer + audio lock

**Files:**
- Modify: `host/src/transport_server.h:21-22`, `host/src/transport_server.cpp:92-107` (`read_hello` grows three out-params)
- Modify: `host/src/stream_daemon.cpp:30-63` and the audio block at `:168-194`
- Test: `host/tests/test_transport_server.cpp` (extend the existing read_hello test if present; otherwise add one)

**Interfaces:**
- Consumes: `decode_hello` (10-arg, Task 1), `select_session_params` (Task 2).
- Produces: `bool TransportServer::read_hello(uint32_t& version, uint32_t& w, uint32_t& h, uint32_t& density, uint32_t& fps, uint8_t& audio_wanted, uint8_t& orientation, std::string& name, std::string& id, int timeout_ms);`

- [ ] **Step 1: Write/extend the failing test** in `host/tests/test_transport_server.cpp`

Add a test that feeds a v4 HELLO through a paired in-memory channel (follow the existing test's channel-setup pattern in that file) and asserts the new out-params:

```cpp
TEST(TransportServer, ReadHelloV4Fields) {
  // ... set up server + client channel exactly as the existing read_hello test does ...
  // client writes: encode_message(Hello, encode_hello(4, 1600, 900, 120, "n", "i", 60, 1, 1))
  uint32_t ver, w, h, d, fps; uint8_t audio, orient; std::string name, id;
  ASSERT_TRUE(server.read_hello(ver, w, h, d, fps, audio, orient, name, id, 1000));
  EXPECT_EQ(fps, 60u); EXPECT_EQ(audio, 1); EXPECT_EQ(orient, 1); EXPECT_EQ(w, 1600u);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j 2>&1 | tail'`
Expected: FAIL — `read_hello` has no 9-value signature.

- [ ] **Step 3: Extend `read_hello`** in `transport_server.h` (declaration) and `transport_server.cpp`:

```cpp
bool TransportServer::read_hello(uint32_t& version, uint32_t& w, uint32_t& h, uint32_t& density,
                                 uint32_t& fps, uint8_t& audio_wanted, uint8_t& orientation,
                                 std::string& name, std::string& id, int timeout_ms) {
  if (!channel_) return false;
  unsigned char buf[1024];
  ParsedMessage m;
  for (;;) {
    if (parser_.next(m)) {
      if (m.type != MsgType::Hello) continue;
      return decode_hello(m.body, version, w, h, density, fps, audio_wanted, orientation, name, id);
    }
    if (!channel_->wait_readable(timeout_ms)) return false;
    ssize_t n = channel_->recv(buf, sizeof(buf));
    if (n <= 0) { close_all(); return false; }
    parser_.feed(buf, static_cast<size_t>(n));
  }
}
```

- [ ] **Step 4: Wire into `stream_daemon.cpp`** — update the HELLO read (line 30-32) and the param selection (lines 56, 62-63) and audio (168-194):

Replace the read at lines 30-32:
```cpp
  uint32_t cver, cw, ch, density, hfps; uint8_t haudio, hori; std::string cname, cid;
  if (!tx_.read_hello(cver, cw, ch, density, hfps, haudio, hori, cname, cid, 10000)) {
    std::fprintf(stderr, "no HELLO\n"); return false; }
  std::fprintf(stderr, "client HELLO v%u %ux%u fps=%u audio=%u orient=%u name=%s id=%s\n",
               cver, cw, ch, hfps, haudio, hori, cname.c_str(), cid.c_str());
  const SessionParams sp = select_session_params(cver, hfps, haudio, hori,
                                                 cfg_.fps, cfg_.audio, cfg_.orientation);
```
Add `#include "session_params.h"` at the top. Use `sp.orientation` where line 56 computes `ocode` (initial): `int ocode = cfg_.live_orientation ? *cfg_.live_orientation : sp.orientation;`. Use `sp.fps` at lines 62-63: `enc_.open(w, h, sp.fps, cfg_.bitrate_kbps)` and `tx_.send_config(w, h, sp.fps, ...)`. Replace `cfg_.audio` at lines 171 and 191 with `sp.audio`.

- [ ] **Step 5: Add the audio claim lock** (in the audio block, ~line 171)

Replace `if (cfg_.audio) { if (audio.start(...)) ... }` with a lock-guarded claim so only one session captures at a time:

```cpp
  // Audio is client-requested (HELLO). Only one session may capture the shared droppix-audio
  // sink at a time: claim an advisory lock; if another session holds it, run video-only.
  int audio_lock_fd = -1;
  bool do_audio = sp.audio;
  if (do_audio) {
    const char* rt = std::getenv("XDG_RUNTIME_DIR");
    std::string lockpath = std::string(rt ? rt : "/tmp") + "/droppix-audio.lock";
    audio_lock_fd = ::open(lockpath.c_str(), O_CREAT | O_RDWR, 0600);
    if (audio_lock_fd < 0 || ::flock(audio_lock_fd, LOCK_EX | LOCK_NB) != 0) {
      if (audio_lock_fd >= 0) { ::close(audio_lock_fd); audio_lock_fd = -1; }
      do_audio = false;                    // another session owns audio
      std::fprintf(stderr, "audio: already claimed by another session; video-only\n");
    }
  }
```
Then replace the two later `cfg_.audio` uses in this block with `do_audio`, and at the end of `run()` (after the stream loop) release: `if (audio_lock_fd >= 0) ::close(audio_lock_fd);` (closing the fd drops the `flock`). Add `#include <sys/file.h>`, `#include <fcntl.h>`, `#include <unistd.h>` if not already present. Also change the frame-timeout line (185) `(cfg_.touch || cfg_.audio)` → `(cfg_.touch || do_audio)`.

- [ ] **Step 6: Build + run host tests**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build --output-on-failure'`
Expected: PASS (all host tests; new TransportServer test green).

- [ ] **Step 7: Commit**

```bash
git add host/src/transport_server.h host/src/transport_server.cpp host/src/stream_daemon.cpp host/tests/test_transport_server.cpp
git commit -m "feat(host): honor client HELLO fps/audio/orientation; lock-arbitrate audio"
```

---

### Task 4: Host GUI — remove the four controls; always-ready audio

**Files:**
- Modify: `host/gui/settings_dialog.cpp` (remove Resolution/FPS/Audio/Orientation widgets + rows + load/store lines)
- Modify: `host/gui/settings_dialog.h:31-38` (remove `resolution_`, `fps_`, `audio_`, `orientation_` members)
- Modify: `host/gui/args_builder.cpp:33` (stop gating `--audio` on `s.audio`)
- Modify: `host/gui/main_window.cpp:273, 660` (always `ensure()` the sink; drop the single-session `s.audio` gate)
- Test: `host/tests/test_args_builder.cpp` (adjust expectations if it asserts on `--audio`)

**Interfaces:**
- Consumes: nothing new. `Settings` keeps its `width/height/fps/audio/orientation` fields (now internal fallbacks); only the UI wiring is removed.

- [ ] **Step 1: Check the args_builder test expectation**

Run: `distrobox enter droppix-dev -- bash -lc 'grep -n "audio" ~/../..$PWD/host/tests/test_args_builder.cpp 2>/dev/null; grep -n "audio" host/tests/test_args_builder.cpp'`
If a test asserts `--audio` appears when `s.audio` is set, update it in Step 4 to reflect that `--audio` is no longer emitted from the GUI (audio is client-driven). If no such assertion exists, no test change is needed.

- [ ] **Step 2: Remove the four rows from `settings_dialog.cpp`**

Delete the widget construction lines (54-59 `resolution_`, `fps_`; 63-67 `orientation_`; 69 `audio_`), the `form->addRow` lines for Resolution/FPS/Orientation/Audio (80-81, 89, 91), and the matching `load`/`store` lines (129, 131-132, 136-137, 146, 148, 152). Leave Source, Bitrate (hidden), Port (hidden), Refresh, Touch, Overlay, Auto-connect, and the App-level section intact. In `store()`, leave `s.width/s.height/s.fps/s.audio/s.orientation` untouched so they keep their `Settings` defaults (the pre-v4 fallbacks).

- [ ] **Step 3: Remove the members** from `settings_dialog.h` (lines 31, 32, 36, 38: `resolution_`, `fps_`, `orientation_`, `audio_`).

- [ ] **Step 4: `args_builder.cpp` — always allow audio capability**

Change line 33 `if (s.audio) a.push_back("--audio");` to always pass it so the streamer can honor a client request:
```cpp
  a.push_back("--audio");   // capability only; the streamer captures iff the client's HELLO asks
```
Update `host/tests/test_args_builder.cpp` if needed so it expects `--audio` unconditionally (for evdi/test sessions alike). Also delete the now-dead orientation arg gate is NOT required (keeping `--orientation` from `s.orientation` default 0 is harmless as a fallback), so leave lines 23-25 alone.

- [ ] **Step 5: `main_window.cpp` — audio sink + single-session gate**

At line 273 the sink is already `ensure()`d per session — confirm it runs unconditionally (not guarded by `s.audio`); if it is guarded, remove the guard so the sink always exists. At line 660 delete `if (sessions_.count() > 0) s.audio = false;` — audio arbitration now lives in the streamer's lockfile (Task 3), so the GUI no longer forces it off.

- [ ] **Step 6: Build + run GUI tests**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build -j && ctest --test-dir ~/droppix-build --output-on-failure'`
Expected: PASS. Also eyeball the Settings dialog has no Resolution/FPS/Audio/Orientation rows (built binary at `~/droppix-build/droppix_gui`).

- [ ] **Step 7: Commit**

```bash
git add host/gui/settings_dialog.cpp host/gui/settings_dialog.h host/gui/args_builder.cpp host/gui/main_window.cpp host/tests/test_args_builder.cpp
git commit -m "feat(host): drop resolution/fps/audio/orientation from GUI (now client-driven)"
```

---

### Task 5: Linux client — persisted settings store

**Files:**
- Create: `client/src/client_settings.h`, `client/src/client_settings.cpp`
- Modify: `client/CMakeLists.txt` (add `src/client_settings.cpp` to `droppix_client_core`; add `tests/test_client_settings.cpp` to `droppix_client_tests`)
- Test: `client/tests/test_client_settings.cpp`

**Interfaces:**
- Produces:
  - `struct ClientSettings { int width=0, height=0; int fps=60; bool audio=false; int rotation=0; };` (`width/height==0` means "use native"; `rotation` is degrees 0/90/180/270).
  - `int rotation_to_code(int degrees);` → `0/90/180/270 → 0/1/2/3` (anything else → 0).
  - `class ClientSettingsStore { public: static ClientSettings load(); static void save(const ClientSettings&); };` — persists via `QSettings` (org `droppix`, app `droppix_client`).

- [ ] **Step 1: Write the failing test** — `client/tests/test_client_settings.cpp`

```cpp
#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QSettings>
#include "client_settings.h"
using namespace droppix;

TEST(ClientSettings, RotationToCode) {
  EXPECT_EQ(rotation_to_code(0), 0);   EXPECT_EQ(rotation_to_code(90), 1);
  EXPECT_EQ(rotation_to_code(180), 2); EXPECT_EQ(rotation_to_code(270), 3);
  EXPECT_EQ(rotation_to_code(45), 0);  // invalid -> 0
}
TEST(ClientSettings, SaveLoadRoundTrip) {
  QSettings::setDefaultFormat(QSettings::IniFormat);   // avoid touching the real config
  ClientSettings s; s.width=1280; s.height=720; s.fps=30; s.audio=true; s.rotation=90;
  ClientSettingsStore::save(s);
  ClientSettings r = ClientSettingsStore::load();
  EXPECT_EQ(r.width,1280); EXPECT_EQ(r.height,720); EXPECT_EQ(r.fps,30);
  EXPECT_TRUE(r.audio); EXPECT_EQ(r.rotation,90);
}
```

The test target needs a `QCoreApplication`; if `droppix_client_tests` uses `gtest_main`, add a tiny `main` wrapper OR set `QT_QPA_PLATFORM=offscreen` and construct a `QCoreApplication` in the test body. Simplest: in the test body, `int argc=0; QCoreApplication app(argc,nullptr);` guarded by a static so it's created once.

- [ ] **Step 2: Run test to verify it fails**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S client -B ~/droppix-client-build -DDROPPIX_CLIENT_BUILD_TESTS=ON && cmake --build ~/droppix-client-build -j 2>&1 | tail'`
Expected: FAIL — `client_settings.h` not found.

- [ ] **Step 3: Implement `client_settings.h`**

```cpp
#pragma once
namespace droppix {
// Per-device display prefs the client sends to the host in HELLO. width/height == 0 means
// "use this device's native screen resolution" (resolved at connect time by the caller).
struct ClientSettings { int width = 0, height = 0; int fps = 60; bool audio = false; int rotation = 0; };
int rotation_to_code(int degrees);   // 0/90/180/270 -> 0/1/2/3; else 0
struct ClientSettingsStore { static ClientSettings load(); static void save(const ClientSettings&); };
}  // namespace droppix
```

- [ ] **Step 4: Implement `client_settings.cpp`**

```cpp
#include "client_settings.h"
#include <QSettings>
namespace droppix {
int rotation_to_code(int d) { switch (d) { case 90: return 1; case 180: return 2; case 270: return 3; default: return 0; } }
ClientSettings ClientSettingsStore::load() {
  QSettings q("droppix", "droppix_client");
  ClientSettings s;
  s.width = q.value("width", 0).toInt();   s.height = q.value("height", 0).toInt();
  s.fps = q.value("fps", 60).toInt();      s.audio = q.value("audio", false).toBool();
  s.rotation = q.value("rotation", 0).toInt();
  return s;
}
void ClientSettingsStore::save(const ClientSettings& s) {
  QSettings q("droppix", "droppix_client");
  q.setValue("width", s.width);   q.setValue("height", s.height);
  q.setValue("fps", s.fps);       q.setValue("audio", s.audio);
  q.setValue("rotation", s.rotation);
}
}  // namespace droppix
```

Add `src/client_settings.cpp` to `droppix_client_core` and `tests/test_client_settings.cpp` to `droppix_client_tests` in `client/CMakeLists.txt`.

- [ ] **Step 5: Run test to verify it passes**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build -R ClientSettings --output-on-failure'`
Expected: PASS (2 tests).

- [ ] **Step 6: Commit**

```bash
git add client/src/client_settings.h client/src/client_settings.cpp client/tests/test_client_settings.cpp client/CMakeLists.txt
git commit -m "feat(client): persisted ClientSettings store (resolution/fps/audio/rotation)"
```

---

### Task 6: Linux client — send settings in HELLO v4

**Files:**
- Modify: `client/src/transport_client.h:65-68`, `client/src/transport_client.cpp:55-65` (`runOverChannel` takes fps/audio/orientation; send v4 HELLO)
- Modify: `client/gui/main_window.cpp:110-137` (`netThreadMain`: resolve native resolution + settings, pass into `runOverChannel`)
- Modify: `client/gui/main_window.h` (hold a `ClientSettings` member)

**Interfaces:**
- Consumes: `ClientSettings`, `rotation_to_code` (Task 5); `encode_hello` v4 (Task 1).
- Produces: `void TransportClient::runOverChannel(ByteChannel&, uint32_t width, uint32_t height, uint32_t density, uint32_t fps, uint8_t audio_wanted, uint8_t orientation_code, StreamListener&, const std::function<bool()>&, const std::string& name, const std::string& id, int pingIntervalMs = 1000);`

- [ ] **Step 1: Extend `runOverChannel`** in `transport_client.h` (add the three params after `density`) and `transport_client.cpp`:

```cpp
void TransportClient::runOverChannel(ByteChannel& channel, uint32_t width, uint32_t height,
                                    uint32_t density, uint32_t fps, uint8_t audio_wanted,
                                    uint8_t orientation_code, StreamListener& listener,
                                    const std::function<bool()>& isRunning,
                                    const std::string& name, const std::string& id,
                                    int pingIntervalMs) {
  {
    std::lock_guard<std::mutex> lk(sendLock_);
    channel_ = &channel;
    auto hello = encode_message(MsgType::Hello,
        encode_hello(kProtocolVersion, width, height, density, name, id,
                     fps, audio_wanted, orientation_code));
    if (!channel.send_all(hello.data(), hello.size())) { channel_ = nullptr; return; }
  }
  // ... rest unchanged ...
}
```

- [ ] **Step 2: Hold settings in `main_window.h`**

Add `#include "client_settings.h"` and a member `ClientSettings settings_ = ClientSettingsStore::load();`.

- [ ] **Step 3: Resolve native resolution + pass settings** in `main_window.cpp` `netThreadMain` (lines 110-137)

Replace the hardcoded `runOverChannel(*channel, 1920, 1080, density, ...)` call. Compute the effective resolution (native when `settings_.width==0`) on the GUI thread before spawning the net thread, or read `QGuiApplication::primaryScreen()` (thread-safe for read) inside:

```cpp
  QSize scr = QGuiApplication::primaryScreen()
                ? QGuiApplication::primaryScreen()->geometry().size() : QSize(1920,1080);
  uint32_t w = settings_.width  > 0 ? settings_.width  : scr.width();
  uint32_t h = settings_.height > 0 ? settings_.height : scr.height();
  uint32_t fps = static_cast<uint32_t>(settings_.fps);
  uint8_t  audio = settings_.audio ? 1 : 0;
  uint8_t  orient = static_cast<uint8_t>(rotation_to_code(settings_.rotation));
  // ... inside the connect loop, replacing the old call:
  client_->runOverChannel(*channel, w, h, density, fps, audio, orient,
                          listener, [this]{ return running_.load(); }, name, id);
```

- [ ] **Step 4: Build + run client tests**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build --output-on-failure'`
Expected: PASS (build green; existing client tests unaffected).

- [ ] **Step 5: Manual smoke — HELLO carries the values**

Run the host streamer + client against localhost and confirm the host logs `client HELLO v4 ... fps=.. audio=.. orient=..` matching the client's saved settings (defaults: native res, 60fps, audio off, 0°). (Full E2E in Task 8.)

- [ ] **Step 6: Commit**

```bash
git add client/src/transport_client.h client/src/transport_client.cpp client/gui/main_window.h client/gui/main_window.cpp
git commit -m "feat(client): send resolution/fps/audio/rotation in HELLO v4 (native-default res)"
```

---

### Task 7: Linux client — Settings dialog + live auto-reconnect

**Files:**
- Create: `client/gui/settings_dialog.h`, `client/gui/settings_dialog.cpp`
- Modify: `client/CMakeLists.txt` (add `gui/settings_dialog.cpp` to `droppix_client`)
- Modify: `client/gui/main_window.h`, `client/gui/main_window.cpp` (Settings toolbar action; apply + reconnect)

**Interfaces:**
- Consumes: `ClientSettings`, `ClientSettingsStore` (Task 5).
- Produces: `class ClientSettingsDialog : public QDialog { public: explicit ClientSettingsDialog(const ClientSettings& current, QString nativeLabel, QWidget* parent=nullptr); ClientSettings result() const; };`

- [ ] **Step 1: Implement `settings_dialog.h`**

```cpp
#pragma once
#include <QDialog>
#include "client_settings.h"
class QComboBox; class QCheckBox;
namespace droppix {
class ClientSettingsDialog : public QDialog {
  Q_OBJECT
 public:
  ClientSettingsDialog(const ClientSettings& current, const QString& nativeLabel,
                       QWidget* parent = nullptr);
  ClientSettings result() const;
 private:
  QComboBox* resolution_; QComboBox* fps_; QCheckBox* audio_; QComboBox* rotation_;
};
}  // namespace droppix
```

- [ ] **Step 2: Implement `settings_dialog.cpp`**

```cpp
#include "settings_dialog.h"
#include <QtWidgets>
namespace droppix {
ClientSettingsDialog::ClientSettingsDialog(const ClientSettings& cur, const QString& nativeLabel,
                                           QWidget* parent) : QDialog(parent) {
  setWindowTitle("Droppix Client — Settings"); setModal(true);
  resolution_ = new QComboBox;
  resolution_->addItem("Native (" + nativeLabel + ")", QSize(0,0));   // width 0 => native
  for (const char* r : {"1280x720","1920x1080","2560x1440","1024x640","800x600"}) {
    const QStringList wh = QString(r).split('x');
    resolution_->addItem(r, QSize(wh[0].toInt(), wh[1].toInt()));
  }
  if (cur.width > 0) { int i = resolution_->findData(QSize(cur.width, cur.height));
    resolution_->setCurrentIndex(i >= 0 ? i : 0); }
  fps_ = new QComboBox; fps_->addItems({"30","60"});
  fps_->setCurrentText(QString::number(cur.fps));
  audio_ = new QCheckBox("Audio"); audio_->setChecked(cur.audio);
  rotation_ = new QComboBox;
  rotation_->addItem("0°",0); rotation_->addItem("90°",90);
  rotation_->addItem("180°",180); rotation_->addItem("270°",270);
  rotation_->setCurrentIndex(std::max(0, rotation_->findData(cur.rotation)));
  auto* form = new QFormLayout;
  form->addRow("Resolution:", resolution_); form->addRow("FPS:", fps_);
  form->addRow("", audio_); form->addRow("Rotation:", rotation_);
  auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
  auto* root = new QVBoxLayout(this); root->addLayout(form); root->addWidget(bb);
}
ClientSettings ClientSettingsDialog::result() const {
  ClientSettings s;
  const QSize wh = resolution_->currentData().toSize();
  s.width = wh.width(); s.height = wh.height();
  s.fps = fps_->currentText().toInt(); s.audio = audio_->isChecked();
  s.rotation = rotation_->currentData().toInt();
  return s;
}
}  // namespace droppix
```

- [ ] **Step 3: Add the toolbar action + apply logic** in `main_window.cpp`

In the `MainWindow` constructor toolbar setup (after the Disconnect action), add:
```cpp
  toolbar->addAction("Settings", this, &MainWindow::onSettingsAction);
```
Declare `void onSettingsAction();` in `main_window.h` (private slot). Implement:
```cpp
void MainWindow::onSettingsAction() {
  QSize scr = QGuiApplication::primaryScreen()
                ? QGuiApplication::primaryScreen()->geometry().size() : QSize(1920,1080);
  ClientSettingsDialog dlg(settings_, QString("%1x%2").arg(scr.width()).arg(scr.height()), this);
  if (dlg.exec() != QDialog::Accepted) return;
  ClientSettings next = dlg.result();
  const bool changed = next.width!=settings_.width || next.height!=settings_.height ||
                       next.fps!=settings_.fps || next.audio!=settings_.audio ||
                       next.rotation!=settings_.rotation;
  settings_ = next; ClientSettingsStore::save(settings_);
  if (changed && running_.load()) {   // apply immediately: reconnect with the new HELLO
    const QString host = currentHost_;
    stopSession();
    startSession(host, lastPort_);
  }
}
```
Add a `quint16 lastPort_` member set in `startSession` so the reconnect reuses the port. Include `settings_dialog.h`.

- [ ] **Step 4: Build**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S client -B ~/droppix-client-build && cmake --build ~/droppix-client-build -j 2>&1 | tail'`
Expected: build succeeds; `~/droppix-client-build/droppix_client` present.

- [ ] **Step 5: Commit**

```bash
git add client/gui/settings_dialog.h client/gui/settings_dialog.cpp client/gui/main_window.h client/gui/main_window.cpp client/CMakeLists.txt
git commit -m "feat(client): Settings dialog + live auto-reconnect on change"
```

---

### Task 8: End-to-end verification

**Files:** none (verification only).

- [ ] **Step 1: Full host+client run**

Build both (`cmake --build ~/droppix-build -j`, `cmake --build ~/droppix-client-build -j`). Start the host GUI, add an evdi monitor session; run `droppix_client` and connect to the host over localhost/LAN.

- [ ] **Step 2: Verify each setting**

Confirm: (a) default connect uses the client's native resolution (host log `client HELLO v4 <native>`); (b) set Resolution 1280×720 → reconnect → host builds a 1280×720 evdi monitor; (c) toggle FPS 60→30 → auto-reconnect → host `send_config` fps=30; (d) enable Audio → host log `audio: capturing`; a second client enabling audio logs `already claimed ... video-only`; (e) set Rotation 90° → host builds a portrait monitor.

- [ ] **Step 3: Back-compat check**

Confirm an un-updated Android build (v3 HELLO) still streams: host log shows `HELLO v3` and it falls back to the host defaults (30fps, audio off, landscape) with no crash.

- [ ] **Step 4: Commit any fixes, then stop**

If Steps 1-3 surface bugs, fix within the relevant task's files and commit. Otherwise this plan is complete; the Android client is the follow-up plan.

---

## Follow-up (separate plan): Android client

Not in this plan. Once the above lands, a second plan updates `android/` to: add a settings section (SharedPreferences) for Resolution (native default) / FPS / Audio; send them + current orientation in HELLO v4 from `StreamActivity`; keep the sensor auto-rotate; reconnect to apply mid-stream. Until then, the host's v3 fallback keeps the current Android app working unchanged.

## Self-review notes

- **Spec coverage:** protocol v4 (T1); host honor + audio arbitration (T2, T3); host GUI removal + always-ready audio (T4); Linux client store (T5), HELLO send + native default (T6), dialog + live reconnect (T7); testing at every task + E2E (T8). Android is explicitly deferred to a follow-up plan (spec's "all clients" satisfied across the two plans; host back-compat keeps Android working meanwhile).
- **Rotation** rides in HELLO for the Linux client (no `ORIENTATION` message, no client-side video rotation) — matches the spec.
- **Audio single-session** enforced by the `droppix-audio.lock` flock (first requester holds it; released on session end) — matches the spec's first-requester-wins.
