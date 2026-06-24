# Touch Input (finger → cursor) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finger touches on the Android tablet drive an absolute cursor (tap=click, drag=drag) on the droppix virtual monitor, via a new INPUT protocol message and a host `uinput` virtual pointer.

**Architecture:** The app sends normalized single-pointer `INPUT` messages; the host (root evdi streamer) maps each to a desktop pixel on the droppix monitor (geometry from `kscreen-doctor`) and injects it through a `uinput` absolute pointer. Pure logic (protocol codec, coordinate mapping, geometry parsing) is unit-tested; the `uinput` device, Android touch path, and end-to-end are build-gated + operator-verified.

**Tech Stack:** C++17 (engine + `uinput`), Kotlin/Android (touch capture), GoogleTest/JUnit, the existing droppix protocol/transport.

## Global Constraints

- **Build/test env:** distrobox `droppix-dev` for C++ (off the CIFS mount): `distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B /home/Spinjitsudoomyt/droppix-build && cmake --build /home/Spinjitsudoomyt/droppix-build -j && ctest --test-dir /home/Spinjitsudoomyt/droppix-build --output-on-failure'`. Android in `droppix-android`: `distrobox enter droppix-android -- bash -lc 'export ANDROID_SDK_ROOT=/home/Spinjitsudoomyt/android-sdk JAVA_HOME=$(dirname $(dirname $(readlink -f $(which java)))) GRADLE_USER_HOME=/home/Spinjitsudoomyt/.droppix-gradle; cd "/var/mnt/nas/Projects/Spacedesk for linux/android"; bash gradlew --no-daemon assembleDebug test'`.
- **C++17**, namespace `droppix`, engine under `host/src`; **Kotlin** package `com.droppix.app`.
- **Protocol (additive, byte-identical both ends):** `MsgType::Input = 7`, app→host. Body big-endian 5 bytes: `u8 action`(0=down,1=move,2=up), `u16 x_norm`(0..65535), `u16 y_norm`(0..65535). No HELLO version bump (additive; unknown type is skipped by an old peer).
- **Injection:** single absolute pointer; `BTN_LEFT` down on touch-down, up on touch-up; `ABS_X/ABS_Y` logical range 0..65535 mapped by libinput across the whole desktop.
- **Privilege:** `uinput` needs root; only the evdi (root, `pkexec`) session injects. Test-pattern session: `/dev/uinput` open fails → display-only, no crash.
- **Droppix identification:** geometry parser selects the enabled output whose mode size == the configured (width,height), preferring a connector/name containing `evdi`/`Unknown`/`droppix`. (Heuristic; validated live.)

---

## File Structure

```
host/src/
  protocol.h  protocol.cpp          # MODIFY: MsgType::Input + encode_input/decode_input
  input_map.h  input_map.cpp        # pure: map normalized touch -> ABS coord (0..65535)
  monitor_geometry.h .cpp           # pure: parse kscreen-doctor -o; pick droppix; desktop bounds
  input_injector.h .cpp             # uinput absolute pointer; inject(action,x,y)
  transport_server.h .cpp           # MODIFY: dispatch INPUT to a handler in poll_control
  stream_daemon.h .cpp              # MODIFY: build injector + wire (evdi path; skip if no uinput)
host/tests/
  test_protocol.cpp                 # MODIFY: INPUT round-trip
  test_input_map.cpp
  test_monitor_geometry.cpp
android/app/src/main/java/com/droppix/app/
  protocol/Protocol.kt              # MODIFY: INPUT(7) + encodeInput
  net/TransportClient.kt            # MODIFY: thread-safe sendInput; expose output stream
  ui/DisplaySurfaceView.kt          # MODIFY: onTouchEvent + TouchListener
  ui/MainActivity.kt                # MODIFY: wire surface touch -> client.sendInput
android/app/src/test/java/com/droppix/app/protocol/ProtocolTest.kt  # MODIFY: encodeInput bytes
```

---

### Task 1: Protocol INPUT (host C++ + Android Kotlin, byte-identical, TDD)

**Files:**
- Modify: `host/src/protocol.h`, `host/src/protocol.cpp`, `host/tests/test_protocol.cpp`
- Modify: `android/app/src/main/java/com/droppix/app/protocol/Protocol.kt`, `android/app/src/test/java/com/droppix/app/protocol/ProtocolTest.kt`

**Interfaces:**
- Produces (C++): `MsgType::Input = 7`; `std::vector<unsigned char> encode_input(uint8_t action, uint16_t x_norm, uint16_t y_norm);` `bool decode_input(const std::vector<unsigned char>& body, uint8_t& action, uint16_t& x_norm, uint16_t& y_norm);`
- Produces (Kotlin): `MsgType.INPUT(7)`; `Protocol.encodeInput(action: Int, xNorm: Int, yNorm: Int): ByteArray`.

- [ ] **Step 1: Write the failing host test** — add to `host/tests/test_protocol.cpp`:

```cpp
TEST(Protocol, InputRoundTrip) {
  auto body = encode_input(0, 30000, 40000);
  uint8_t a; uint16_t x, y;
  ASSERT_TRUE(decode_input(body, a, x, y));
  EXPECT_EQ(a, 0); EXPECT_EQ(x, 30000); EXPECT_EQ(y, 40000);
}
TEST(Protocol, InputWireLayout) {
  auto m = encode_message(MsgType::Input, encode_input(2, 0x0102, 0x0304));
  // len = 1(type)+5(body)=6; type=7; body = 02 0102 0304 (big-endian)
  ASSERT_EQ(m.size(), 4u + 6u);
  EXPECT_EQ(m[3], 6); EXPECT_EQ(m[4], 7);
  EXPECT_EQ(m[5], 0x02);
  EXPECT_EQ(m[6], 0x01); EXPECT_EQ(m[7], 0x02);
  EXPECT_EQ(m[8], 0x03); EXPECT_EQ(m[9], 0x04);
}
TEST(Protocol, InputTooShortInvalid) {
  uint8_t a; uint16_t x, y;
  EXPECT_FALSE(decode_input({0, 0}, a, x, y));
}
```

- [ ] **Step 2: Build host, verify failure** (encode_input undefined).

- [ ] **Step 3: Implement in protocol.h/.cpp.** In `protocol.h`, add `Input = 7` to the `MsgType` enum and declare:

```cpp
std::vector<unsigned char> encode_input(uint8_t action, uint16_t x_norm, uint16_t y_norm);
bool decode_input(const std::vector<unsigned char>& body,
                  uint8_t& action, uint16_t& x_norm, uint16_t& y_norm);
```
In `protocol.cpp` add (reuse the existing big-endian helpers; add `put_u16`/`get_u16` if absent):

```cpp
static void put_u16(std::vector<unsigned char>& v, uint16_t x) {
  v.push_back((x >> 8) & 0xFF); v.push_back(x & 0xFF);
}
static uint16_t get_u16(const unsigned char* p) {
  return (uint16_t(p[0]) << 8) | uint16_t(p[1]);
}

std::vector<unsigned char> encode_input(uint8_t action, uint16_t x_norm, uint16_t y_norm) {
  std::vector<unsigned char> b;
  b.push_back(action);
  put_u16(b, x_norm); put_u16(b, y_norm);
  return b;
}
bool decode_input(const std::vector<unsigned char>& b,
                  uint8_t& action, uint16_t& x_norm, uint16_t& y_norm) {
  if (b.size() != 5) return false;
  action = b[0];
  x_norm = get_u16(b.data() + 1);
  y_norm = get_u16(b.data() + 3);
  return true;
}
```
(If `put_u16`/`get_u16` already exist as anonymous-namespace helpers, don't duplicate — reuse them.)

- [ ] **Step 4: Build host + run; INPUT tests pass** with all prior.

- [ ] **Step 5: Write the failing Kotlin test** — add to `ProtocolTest.kt`:

```kotlin
@Test fun encodeInputMatchesHostWireFormat() {
  // action=2, x=0x0102, y=0x0304 ; encodeMessage adds [00 00 00 06][07]
  val m = Protocol.encodeMessage(MsgType.INPUT, Protocol.encodeInput(2, 0x0102, 0x0304))
  assertArrayEquals(
    byteArrayOf(0,0,0,6, 7, 0x02, 0x01, 0x02, 0x03, 0x04), m)
}
```

- [ ] **Step 6: Run Kotlin tests, verify failure** (INPUT/encodeInput undefined).

- [ ] **Step 7: Implement in Protocol.kt.** Add `INPUT(7)` to the `MsgType` enum, and:

```kotlin
fun encodeInput(action: Int, xNorm: Int, yNorm: Int): ByteArray {
    val out = ArrayList<Byte>(5)
    out.add(action.toByte())
    out.add((xNorm ushr 8).toByte()); out.add(xNorm.toByte())
    out.add((yNorm ushr 8).toByte()); out.add(yNorm.toByte())
    return out.toByteArray()
}
```

- [ ] **Step 8: Run Kotlin tests; pass.**

- [ ] **Step 9: Commit**

```bash
git add host/src/protocol.h host/src/protocol.cpp host/tests/test_protocol.cpp android/app/src/main/java/com/droppix/app/protocol/Protocol.kt android/app/src/test/java/com/droppix/app/protocol/ProtocolTest.kt
git commit -m "feat(protocol): INPUT message (action + normalized x,y), both ends"
```

---

### Task 2: Coordinate mapping + monitor-geometry parser (pure, TDD)

**Files:**
- Create: `host/src/input_map.h`, `host/src/input_map.cpp`, `host/tests/test_input_map.cpp`
- Create: `host/src/monitor_geometry.h`, `host/src/monitor_geometry.cpp`, `host/tests/test_monitor_geometry.cpp`
- Modify: `host/CMakeLists.txt` (sources → `droppix_core`; tests → `droppix_tests`)

**Interfaces:**
- Produces:
  - `struct droppix::Rect { int x=0, y=0, w=0, h=0; };`
  - `struct droppix::AbsCoord { int x=0, y=0; };`
  - `droppix::AbsCoord droppix::map_to_abs(uint16_t x_norm, uint16_t y_norm, const Rect& monitor, int desktop_w, int desktop_h);`
  - `struct droppix::OutputInfo { std::string name; Rect geom; bool enabled = false; };`
  - `std::vector<OutputInfo> droppix::parse_kscreen_outputs(const std::string& text);`
  - `droppix::Rect droppix::desktop_bounds(const std::vector<OutputInfo>& outs);`
  - `bool droppix::select_droppix(const std::vector<OutputInfo>& outs, int mode_w, int mode_h, Rect& out);`

- [ ] **Step 1: Write the failing input_map test** — `host/tests/test_input_map.cpp`:

```cpp
#include <gtest/gtest.h>
#include "input_map.h"
using namespace droppix;

TEST(InputMap, SingleMonitorAtOriginIsIdentity) {
  // monitor == desktop -> abs == input norm (within rounding)
  AbsCoord c = map_to_abs(30000, 40000, Rect{0,0,1920,1080}, 1920, 1080);
  EXPECT_EQ(c.x, 30000);
  EXPECT_EQ(c.y, 40000);
}
TEST(InputMap, OffsetMonitorMapsIntoDesktop) {
  // droppix monitor on the right half of a 3840x1080 desktop
  Rect mon{1920,0,1920,1080};
  EXPECT_EQ(map_to_abs(0,     0, mon, 3840, 1080).x, 32768);  // left edge of monitor
  EXPECT_EQ(map_to_abs(65535, 0, mon, 3840, 1080).x, 65535);  // right edge of desktop
  EXPECT_EQ(map_to_abs(0,     0, mon, 3840, 1080).y, 0);
}
```

- [ ] **Step 2: Add to CMake, build, verify failure.**

- [ ] **Step 3: Implement input_map.h/.cpp.**

`host/src/input_map.h`:

```cpp
#pragma once
#include <cstdint>
namespace droppix {
struct Rect { int x = 0, y = 0, w = 0, h = 0; };
struct AbsCoord { int x = 0, y = 0; };
// Map a normalized touch (0..65535 within `monitor`) to a uinput ABS value
// (0..65535) spanning the whole desktop.
AbsCoord map_to_abs(uint16_t x_norm, uint16_t y_norm,
                    const Rect& monitor, int desktop_w, int desktop_h);
}  // namespace droppix
```

`host/src/input_map.cpp`:

```cpp
#include "input_map.h"
#include <cmath>
namespace droppix {
static int map_axis(uint16_t norm, int off, int size, int desktop) {
  if (desktop <= 0) return 0;
  const double frac = norm / 65535.0;
  const double global = off + frac * size;
  int v = static_cast<int>(std::lround(global / desktop * 65535.0));
  if (v < 0) v = 0;
  if (v > 65535) v = 65535;
  return v;
}
AbsCoord map_to_abs(uint16_t x_norm, uint16_t y_norm,
                    const Rect& monitor, int desktop_w, int desktop_h) {
  return AbsCoord{ map_axis(x_norm, monitor.x, monitor.w, desktop_w),
                   map_axis(y_norm, monitor.y, monitor.h, desktop_h) };
}
}  // namespace droppix
```

- [ ] **Step 4: Build + test → input_map passes.**

- [ ] **Step 5: Write the failing geometry test** — `host/tests/test_monitor_geometry.cpp`:

```cpp
#include <gtest/gtest.h>
#include "monitor_geometry.h"
using namespace droppix;

static const char* kSample =
  "Output: 1 HDMI-A-3 333136d1\n"
  "\tenabled\n"
  "\tGeometry: 0,0 1600x900\n"
  "Output: 2 DP-2 fef981ef\n"
  "\tenabled\n"
  "\tGeometry: 1600,0 1920x1080\n"
  "Output: 3 Unknown-1 abcd\n"
  "\tenabled\n"
  "\tGeometry: 3520,0 2560x1600\n";

TEST(MonitorGeometry, ParsesOutputs) {
  auto outs = parse_kscreen_outputs(kSample);
  ASSERT_EQ(outs.size(), 3u);
  EXPECT_EQ(outs[0].name, "HDMI-A-3");
  EXPECT_TRUE(outs[2].enabled);
  EXPECT_EQ(outs[2].geom.x, 3520);
  EXPECT_EQ(outs[2].geom.w, 2560);
  EXPECT_EQ(outs[2].geom.h, 1600);
}
TEST(MonitorGeometry, DesktopBounds) {
  Rect b = desktop_bounds(parse_kscreen_outputs(kSample));
  EXPECT_EQ(b.w, 3520 + 2560);   // 6080
  EXPECT_EQ(b.h, 1600);
}
TEST(MonitorGeometry, SelectsDroppixBySizeMatch) {
  Rect r;
  ASSERT_TRUE(select_droppix(parse_kscreen_outputs(kSample), 2560, 1600, r));
  EXPECT_EQ(r.x, 3520); EXPECT_EQ(r.w, 2560); EXPECT_EQ(r.h, 1600);
}
TEST(MonitorGeometry, SelectFailsWhenNoMatch) {
  Rect r;
  EXPECT_FALSE(select_droppix(parse_kscreen_outputs(kSample), 1234, 5678, r));
}
```

- [ ] **Step 6: Add to CMake, build, verify failure.**

- [ ] **Step 7: Implement monitor_geometry.h/.cpp.**

`host/src/monitor_geometry.h`:

```cpp
#pragma once
#include <string>
#include <vector>
#include "input_map.h"   // droppix::Rect
namespace droppix {
struct OutputInfo { std::string name; Rect geom; bool enabled = false; };
std::vector<OutputInfo> parse_kscreen_outputs(const std::string& text);
Rect desktop_bounds(const std::vector<OutputInfo>& outs);   // {0,0,maxRight,maxBottom}
// Select the droppix output: an enabled output whose size == mode, preferring a
// name containing evdi/Unknown/droppix. Returns false if none match.
bool select_droppix(const std::vector<OutputInfo>& outs, int mode_w, int mode_h, Rect& out);
}  // namespace droppix
```

`host/src/monitor_geometry.cpp`:

```cpp
#include "monitor_geometry.h"
#include <sstream>
#include <cstdio>

namespace droppix {

std::vector<OutputInfo> parse_kscreen_outputs(const std::string& text) {
  std::vector<OutputInfo> outs;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    auto pos = line.find("Output:");
    if (pos != std::string::npos) {
      // "Output: <num> <name> ..."
      std::istringstream ls(line.substr(pos + 7));
      int num; std::string name;
      ls >> num >> name;
      OutputInfo o; o.name = name; outs.push_back(o);
      continue;
    }
    if (outs.empty()) continue;
    if (line.find("enabled") != std::string::npos &&
        line.find("disabled") == std::string::npos) {
      outs.back().enabled = true;
    }
    auto gp = line.find("Geometry:");
    if (gp != std::string::npos) {
      int x, y, w, h;
      // "Geometry: X,Y WxH"
      if (std::sscanf(line.c_str() + gp, "Geometry: %d,%d %dx%d", &x, &y, &w, &h) == 4) {
        outs.back().geom = Rect{x, y, w, h};
      }
    }
  }
  return outs;
}

Rect desktop_bounds(const std::vector<OutputInfo>& outs) {
  int right = 0, bottom = 0;
  for (const auto& o : outs) {
    if (!o.enabled) continue;
    right = std::max(right, o.geom.x + o.geom.w);
    bottom = std::max(bottom, o.geom.y + o.geom.h);
  }
  return Rect{0, 0, right, bottom};
}

bool select_droppix(const std::vector<OutputInfo>& outs, int mode_w, int mode_h, Rect& out) {
  const OutputInfo* sized = nullptr;
  const OutputInfo* preferred = nullptr;
  for (const auto& o : outs) {
    if (!o.enabled) continue;
    if (o.geom.w == mode_w && o.geom.h == mode_h) {
      if (!sized) sized = &o;
      if (o.name.find("evdi") != std::string::npos ||
          o.name.find("Unknown") != std::string::npos ||
          o.name.find("droppix") != std::string::npos) {
        preferred = &o; break;
      }
    }
  }
  const OutputInfo* pick = preferred ? preferred : sized;
  if (!pick) return false;
  out = pick->geom;
  return true;
}
}  // namespace droppix
```
(Add `#include <algorithm>` for `std::max`.)

- [ ] **Step 8: Build + test → geometry passes** with all prior.

- [ ] **Step 9: Commit**

```bash
git add host/src/input_map.* host/src/monitor_geometry.* host/tests/test_input_map.cpp host/tests/test_monitor_geometry.cpp host/CMakeLists.txt
git commit -m "feat(input): coordinate mapping + kscreen geometry parser (pure, tested)"
```

---

### Task 3: InputInjector (uinput) + TransportServer/StreamDaemon wiring (build-gate + operator)

**Files:**
- Create: `host/src/input_injector.h`, `host/src/input_injector.cpp`
- Modify: `host/src/transport_server.h`, `host/src/transport_server.cpp`
- Modify: `host/src/stream_daemon.h`, `host/src/stream_daemon.cpp`
- Modify: `host/CMakeLists.txt`

**Interfaces:**
- Consumes: `map_to_abs`, `Rect` (Task 2), `MsgType::Input`/`decode_input` (Task 1).
- Produces:
  - `class droppix::InputInjector { public: bool open(const Rect& monitor, int desktop_w, int desktop_h); void inject(uint8_t action, uint16_t x_norm, uint16_t y_norm); ~InputInjector(); bool ok() const; };`
  - `void TransportServer::set_input_handler(std::function<void(uint8_t,uint16_t,uint16_t)> h);` — called for each INPUT message during `poll_control`.

- [ ] **Step 1: Write input_injector.h**

```cpp
#pragma once
#include <cstdint>
#include "input_map.h"
namespace droppix {
// Absolute uinput pointer; maps normalized touch onto the droppix monitor.
class InputInjector {
 public:
  ~InputInjector();
  bool open(const Rect& monitor, int desktop_w, int desktop_h);  // needs root /dev/uinput
  bool ok() const { return fd_ >= 0; }
  void inject(uint8_t action, uint16_t x_norm, uint16_t y_norm);
 private:
  int fd_ = -1;
  Rect monitor_;
  int desktop_w_ = 0, desktop_h_ = 0;
};
}  // namespace droppix
```

- [ ] **Step 2: Write input_injector.cpp**

```cpp
#include "input_injector.h"
#include <linux/uinput.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstring>
#include <cstdio>

namespace droppix {
namespace {
void emit(int fd, int type, int code, int val) {
  input_event ev{};
  ev.type = type; ev.code = code; ev.value = val;
  ::write(fd, &ev, sizeof(ev));
}
}  // namespace

bool InputInjector::open(const Rect& monitor, int desktop_w, int desktop_h) {
  monitor_ = monitor; desktop_w_ = desktop_w; desktop_h_ = desktop_h;
  fd_ = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (fd_ < 0) { std::fprintf(stderr, "uinput open failed (need root); input disabled\n"); return false; }

  ioctl(fd_, UI_SET_EVBIT, EV_KEY);
  ioctl(fd_, UI_SET_KEYBIT, BTN_LEFT);
  ioctl(fd_, UI_SET_EVBIT, EV_ABS);
  ioctl(fd_, UI_SET_ABSBIT, ABS_X);
  ioctl(fd_, UI_SET_ABSBIT, ABS_Y);

  uinput_abs_setup ax{}; ax.code = ABS_X; ax.absinfo.minimum = 0; ax.absinfo.maximum = 65535;
  ioctl(fd_, UI_ABS_SETUP, &ax);
  uinput_abs_setup ay{}; ay.code = ABS_Y; ay.absinfo.minimum = 0; ay.absinfo.maximum = 65535;
  ioctl(fd_, UI_ABS_SETUP, &ay);

  uinput_setup us{};
  us.id.bustype = BUS_USB; us.id.vendor = 0x1209; us.id.product = 0xd701;
  std::strncpy(us.name, "droppix-touch", sizeof(us.name) - 1);
  if (ioctl(fd_, UI_DEV_SETUP, &us) < 0 || ioctl(fd_, UI_DEV_CREATE) < 0) {
    std::fprintf(stderr, "uinput device create failed; input disabled\n");
    ::close(fd_); fd_ = -1; return false;
  }
  return true;
}

void InputInjector::inject(uint8_t action, uint16_t x_norm, uint16_t y_norm) {
  if (fd_ < 0) return;
  AbsCoord c = map_to_abs(x_norm, y_norm, monitor_, desktop_w_, desktop_h_);
  emit(fd_, EV_ABS, ABS_X, c.x);
  emit(fd_, EV_ABS, ABS_Y, c.y);
  if (action == 0) emit(fd_, EV_KEY, BTN_LEFT, 1);       // down
  else if (action == 2) emit(fd_, EV_KEY, BTN_LEFT, 0);  // up
  emit(fd_, EV_SYN, SYN_REPORT, 0);
}

InputInjector::~InputInjector() {
  if (fd_ >= 0) { ioctl(fd_, UI_DEV_DESTROY); ::close(fd_); }
}
}  // namespace droppix
```

- [ ] **Step 3: Extend TransportServer to dispatch INPUT.** In `transport_server.h` add a member + setter:

```cpp
#include <functional>
// ... in class:
  void set_input_handler(std::function<void(uint8_t,uint16_t,uint16_t)> h) { input_handler_ = std::move(h); }
 private:
  std::function<void(uint8_t,uint16_t,uint16_t)> input_handler_;
```
In `transport_server.cpp`, in `poll_control()`'s message loop (where `MsgType::Ping` is handled), add an INPUT branch:

```cpp
    if (m.type == MsgType::Ping) {
      send_all(encode_message(MsgType::Pong, m.body));
    } else if (m.type == MsgType::Input && input_handler_) {
      uint8_t a; uint16_t x, y;
      if (decode_input(m.body, a, x, y)) input_handler_(a, x, y);
    }
```

- [ ] **Step 4: Wire into StreamDaemon (evdi path; skip gracefully otherwise).** In `stream_daemon.cpp`, add includes (`input_injector.h`, `monitor_geometry.h`, `<array>`, `<cstdio>`), a helper to capture `kscreen-doctor -o`, and create+wire the injector after the source starts. Add near the top of `stream_daemon.cpp`:

```cpp
static std::string run_kscreen() {
  std::string out;
  FILE* p = popen("kscreen-doctor -o 2>/dev/null", "r");
  if (!p) return out;
  char buf[4096]; size_t n;
  while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
  pclose(p);
  return out;
}
```
In `run_until`, after `src_.start(w,h)` succeeds and before the loop, attempt to set up input (no-op if it can't):

```cpp
  InputInjector injector;
  {
    Rect mon;
    auto outs = parse_kscreen_outputs(run_kscreen());
    if (select_droppix(outs, w, h, mon)) {
      Rect db = desktop_bounds(outs);
      if (injector.open(mon, db.w, db.h)) {
        tx_.set_input_handler([&injector](uint8_t a, uint16_t x, uint16_t y) {
          injector.inject(a, x, y);
        });
        std::fprintf(stderr, "input: injecting into %dx%d at (%d,%d), desktop %dx%d\n",
                     mon.w, mon.h, mon.x, mon.y, db.w, db.h);
      }
    } else {
      std::fprintf(stderr, "input: droppix output not found; input disabled\n");
    }
  }
```
(`injector` lives for the whole session; the handler captures it by reference. It's destroyed when `run_until` returns, before the next session — and `set_input_handler` is reset each session. Add `#include "input_map.h"` for `Rect`.)

- [ ] **Step 5: Wire CMake.** Add `src/input_injector.cpp` to `droppix_core` (input_map.cpp + monitor_geometry.cpp added in Task 2). Build.

- [ ] **Step 6: Build → links; all prior tests pass.** No new unit test (uinput needs root + a live session). The gate is clean compile+link + no regression. Confirm `droppix_stream` links.

- [ ] **Step 7: Commit**

```bash
git add host/src/input_injector.* host/src/transport_server.h host/src/transport_server.cpp host/src/stream_daemon.h host/src/stream_daemon.cpp host/CMakeLists.txt
git commit -m "feat(input): uinput injector + INPUT dispatch + evdi-session wiring"
```

---

### Task 4: Android touch capture + sendInput (build-gate + operator)

**Files:**
- Modify: `android/app/src/main/java/com/droppix/app/ui/DisplaySurfaceView.kt`
- Modify: `android/app/src/main/java/com/droppix/app/net/TransportClient.kt`
- Modify: `android/app/src/main/java/com/droppix/app/ui/MainActivity.kt`

**Interfaces:**
- Consumes: `Protocol.encodeInput`, `MsgType.INPUT` (Task 1).
- Produces:
  - `DisplaySurfaceView`: `interface TouchListener { fun onTouch(action: Int, xNorm: Int, yNorm: Int) }`; `fun setTouchListener(l: TouchListener?)`; `onTouchEvent` override.
  - `TransportClient.sendInput(action: Int, xNorm: Int, yNorm: Int)` — thread-safe; no-op if not connected.

- [ ] **Step 1: Add touch capture to DisplaySurfaceView.** In `DisplaySurfaceView.kt`, add the interface, a listener field, and `onTouchEvent`:

```kotlin
    interface TouchListener { fun onTouch(action: Int, xNorm: Int, yNorm: Int) }
    private var touchListener: TouchListener? = null
    fun setTouchListener(l: TouchListener?) { touchListener = l }

    override fun onTouchEvent(event: android.view.MotionEvent): Boolean {
        val l = touchListener ?: return false
        val action = when (event.actionMasked) {
            android.view.MotionEvent.ACTION_DOWN -> 0
            android.view.MotionEvent.ACTION_MOVE -> 1
            android.view.MotionEvent.ACTION_UP, android.view.MotionEvent.ACTION_CANCEL -> 2
            else -> return false
        }
        val w = width.coerceAtLeast(1); val h = height.coerceAtLeast(1)
        val xn = ((event.x / w).coerceIn(0f, 1f) * 65535f).toInt()
        val yn = ((event.y / h).coerceIn(0f, 1f) * 65535f).toInt()
        l.onTouch(action, xn, yn)
        return true
    }
```

- [ ] **Step 2: Make TransportClient.sendInput thread-safe.** In `TransportClient.kt`, add members and a send method; set/clear the stream in `run()`:

```kotlin
    private val sendLock = Any()
    @Volatile private var out: java.io.OutputStream? = null

    fun sendInput(action: Int, xNorm: Int, yNorm: Int) {
        val o = out ?: return
        val msg = Protocol.encodeMessage(MsgType.INPUT, Protocol.encodeInput(action, xNorm, yNorm))
        synchronized(sendLock) {
            try { o.write(msg); o.flush() } catch (e: Exception) { /* dropped; loop will close */ }
        }
    }
```
In `run()`, change the local `val out = socket.getOutputStream()` to assign the member, guard the existing HELLO/PING/PONG writes with the same lock, and clear on exit. Concretely: replace `val out = socket.getOutputStream()` with:
```kotlin
            val outStream = socket.getOutputStream()
            out = outStream
```
then change every `out.write(...)`/`out.flush()` in `run()` to use `synchronized(sendLock) { outStream.write(...); outStream.flush() }`, and in the `finally` block add `out = null` before closing the socket.

- [ ] **Step 3: Wire MainActivity.** In `startStreaming()`, after the surface is available, set the touch listener so it forwards to the running client. Add a field `@Volatile private var client: TransportClient? = null`, assign it where the client is created in the net thread, and in `onSurfaceReady` (UI thread) set:
```kotlin
        surfaceView.setTouchListener(object : DisplaySurfaceView.TouchListener {
            override fun onTouch(action: Int, xNorm: Int, yNorm: Int) {
                client?.sendInput(action, xNorm, yNorm)
            }
        })
```
In `onSurfaceGone`/`onPause`, call `surfaceView.setTouchListener(null)` and clear `client`. (In the net thread, set `client = TransportClient()` before `client!!.run(...)`, and `client = null` after the loop.)

- [ ] **Step 4: Build (APK gate) + tests**

```
distrobox enter droppix-android -- bash -lc 'export ANDROID_SDK_ROOT=/home/Spinjitsudoomyt/android-sdk JAVA_HOME=$(dirname $(dirname $(readlink -f $(which java)))) GRADLE_USER_HOME=/home/Spinjitsudoomyt/.droppix-gradle; cd "/var/mnt/nas/Projects/Spacedesk for linux/android"; bash gradlew --no-daemon assembleDebug test'
```
Expected: `BUILD SUCCESSFUL`, APK produced, all unit tests pass (incl. the Task 1 `encodeInputMatchesHostWireFormat`). No UI unit test (Android runtime); touch behavior is operator-verified.

- [ ] **Step 5: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/ui/DisplaySurfaceView.kt android/app/src/main/java/com/droppix/app/net/TransportClient.kt android/app/src/main/java/com/droppix/app/ui/MainActivity.kt
git commit -m "feat(android): capture touch and send INPUT to the host"
```

---

### Task 5: Operator end-to-end touch test

This step is performed by the human operator (needs the device + the root evdi session + GUI).

- [ ] **Step 1: Rebuild + reinstall.** Rebuild the host (`droppix_stream`) and the Android APK; `adb install -r /home/Spinjitsudoomyt/droppix-android-build/app/outputs/apk/debug/app-debug.apk`.
- [ ] **Step 2: Start the evdi session** (root, so uinput works) via the GUI (`droppix_gui` → Real monitor → Start, `pkexec`) or `sudo droppix_stream --port 27000 ...`; `adb reverse tcp:27000 tcp:27000`; launch the app.
- [ ] **Step 3: Verify the host log** shows `input: injecting into WxH at (x,y), desktop ...` (the droppix output was found + uinput opened). If it shows "droppix output not found", note the actual `kscreen-doctor -o` block so the selection heuristic can be adjusted.
- [ ] **Step 4: Touch test.** On the tablet: a single tap → the cursor jumps to that point on the droppix monitor and clicks; drag → moves/drags; tap a button in a real app → it activates. Confirm the mapping is correct across all four corners (no offset/scaling error).
- [ ] **Step 5: Record findings** in `docs/superpowers/specs/2026-06-24-touch-input-findings.md` (mapping accuracy, latency feel, any output-identification issue).

```bash
git add docs/superpowers/specs/2026-06-24-touch-input-findings.md
git commit -m "docs: touch input operator findings"
```

---

## Self-Review

**1. Spec coverage:** Protocol INPUT both ends (Task 1); `map_to_abs` + `MonitorGeometry` parser (Task 2); `InputInjector` uinput + `TransportServer` INPUT dispatch + `StreamDaemon` evdi-session wiring with graceful display-only fallback (Task 3); Android touch capture + thread-safe `sendInput` + wiring (Task 4); operator end-to-end (Task 5). Direct/absolute single-pointer (down=BTN_LEFT 1, up=0); evdi-only injection (uinput-open failure → display-only); droppix identification by size-match + name preference. Out-of-scope items (stylus/pressure, multi-touch, keyboard, test-pattern injection) excluded.

**2. Placeholder scan:** No TBD/TODO; every code step has complete code. The findings doc (Task 5) is filled from the operator run. The MainActivity wiring step references the existing reconnect-loop structure with concrete field additions.

**3. Type consistency:** `MsgType::Input`/`encode_input`/`decode_input` (C++) and `MsgType.INPUT`/`encodeInput` (Kotlin) consistent and byte-identical (asserted by `InputWireLayout` + `encodeInputMatchesHostWireFormat`). `Rect`/`AbsCoord`/`map_to_abs` defined in input_map.h, consumed by `monitor_geometry.h` and `InputInjector`. `OutputInfo`/`parse_kscreen_outputs`/`desktop_bounds`/`select_droppix` consistent between Task 2 and Task 3 usage. `TransportServer::set_input_handler(std::function<void(uint8_t,uint16_t,uint16_t)>)` matches the StreamDaemon lambda and the `decode_input` out-params. `DisplaySurfaceView.TouchListener.onTouch(Int,Int,Int)` + `TransportClient.sendInput(Int,Int,Int)` consistent with the MainActivity wiring. Normalized space is 0..65535 end-to-end (Android encode, host map).

**Known risks (flagged, by design):** (a) droppix-output identification is a size-match heuristic — Task 5 Step 3 captures the real layout if it misses; (b) libinput mapping an `ABS_X/Y`+`BTN_LEFT` device across the whole desktop is the QEMU-usb-tablet recipe and expected to work, but is the key live-validation point in Task 5 Step 4.
