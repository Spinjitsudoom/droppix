# WiFi + Discovery — Part 2 Implementation Plan (PC scans for tablets + wake)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the PC's droppix GUI discover tablets on the network and start a session by clicking one — the PC sends a UDP "wake" and the tablet dials back (streaming direction unchanged).

**Architecture:** The tablet advertises `_droppix-client._tcp` over NSD (its service port = a UDP "wake" port it listens on) and runs a wake listener. The host GUI browses `_droppix-client._tcp` via `avahi-browse`, lists tablets, and on Connect starts the streamer (if needed) and sends a one-shot UDP datagram `DPXW`+`<stream-port>` to the tablet; the tablet shows a "Connect to `<ip>`?" confirm and then opens the stream socket back to the PC (which approves it automatically since it initiated).

**Tech Stack:** C++17 (host, GoogleTest, CMake), Qt6 Widgets + Qt6::Network (host GUI), Kotlin/Android (minSdk 21, NSD, DatagramSocket), Avahi CLI (`avahi-browse`).

**Spec:** `docs/superpowers/specs/2026-06-27-wifi-discovery-client-gui-design.md` (Part 2 sections).
**Builds on:** Part 1 (merged `ec0fc28`).

## Global Constraints

- Host C++ builds/tests in the `droppix-dev` distrobox; build dir OFF the CIFS mount at
  `/home/Spinjitsudoomyt/droppix-build`. New source/test files require a `cmake -S host -B <build>` reconfigure.
- Android builds in the `droppix-android` distrobox: `ANDROID_SDK_ROOT=/home/Spinjitsudoomyt/android-sdk`,
  `bash gradlew --no-daemon` from `android/`; both `:app:assembleDebug` and `:app:testDebugUnitTest` must pass.
- minSdk 21 (Nexus 10 / API 22). No Jetpack Compose — Material Views only. NSD classic
  `discoverServices`/`registerService`/`resolveService` APIs (deprecated in 34 but required for 21).
- Wake datagram wire format: ASCII magic `DPXW` (4 bytes) then `u16` port, big-endian = 6 bytes total.
  Host and Kotlin codecs must be byte-identical (assert in tests).
- `git merge` on this mount intermittently errors `fatal: stash failed`; run each merge as its own
  standalone command with `--no-autostash` (never chain `checkout && merge`).
- Commit author `Claude <noreply@anthropic.com>`; message body ends with
  `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`. Do not merge/switch branches in tasks — commit on the work branch.

## File Structure

- `host/src/wake.h` (new, header-only, pure) — `encode_wake(port)` / `decode_wake(bytes,port)`.
- `host/tests/test_wake.cpp` (new) — wake codec tests.
- `host/src/mdns_browse.{h,cpp}` (new, pure parser) — parse `avahi-browse -rptk` output → `[{name,address,port}]`.
- `host/tests/test_mdns_browse.cpp` (new).
- `host/gui/mdns_browser.{h,cpp}` (new) — `QProcess`/timer wrapper emitting the parsed device list.
- `host/gui/main_window.{h,cpp}` — Devices panel (list + Connect) + `QUdpSocket` wake send.
- `host/gui/stream_controller.cpp` + `host/src/stream_daemon.cpp` — Part-1 follow-up (approve-request field order + parser).
- `android/.../net/Wake.kt` (new) — `encodeWake`/`decodeWake` (decode used by the listener; encode for tests).
- `android/.../net/WakeService.kt` (new) — NSD register `_droppix-client._tcp` + UDP wake listener.
- `android/.../ui/ConnectActivity.kt` — host the WakeService while visible; confirm dialog → `connectTo`.
- `android/.../net/Discovery.kt`, `android/.../ui/StreamActivity.kt` — Part-1 follow-ups.

---

### Task 1: Part-1 review follow-ups (3 minor cleanups)

**Files:**
- Modify: `host/src/stream_daemon.cpp` (approve-request format), `host/gui/stream_controller.cpp` (`parseApproveRequest`)
- Modify: `android/.../net/Discovery.kt` (main-thread queue), `android/.../ui/StreamActivity.kt` (redundant save)

**Interfaces:** none new (behavior-preserving cleanups).

- [ ] **Step 1: Reorder the approve-request fields so `name` is last (free-form-safe).** In
`stream_daemon.cpp`, change the emit to `"approve-request id=%s ip=%s name=%s\n"` (ip before name).

- [ ] **Step 2: Update the GUI parser to match.** In `stream_controller.cpp` `parseApproveRequest`,
parse: `id` = between `"id="` and `" ip="`; `ip` = between `" ip="` and `" name="`; `name` =
everything after `" name="` (so a spaced/odd name can't corrupt the parse):

```cpp
const int idPos = rest.indexOf("id="); const int ipPos = rest.indexOf(" ip=");
const int namePos = rest.indexOf(" name=");
if (idPos < 0 || ipPos < 0 || namePos < 0) return false;
id   = rest.mid(idPos + 3, ipPos - (idPos + 3));
ip   = rest.mid(ipPos + 4, namePos - (ipPos + 4));
name = rest.mid(namePos + 6);
```

- [ ] **Step 3: Discovery.kt — serialize the resolve queue on the main thread.** Post
`enqueueResolve`/`maybeStartNextResolve` (the `resolveQueue`/`resolveInFlight` mutations) through the
existing `mainHandler.post { ... }` in the NSD callbacks, so the queue lives entirely on the main thread.

- [ ] **Step 4: StreamActivity — drop the redundant last-endpoint save** (keep only `ConnectActivity.connectTo`'s
write; remove the duplicate save in `onConfig`).

- [ ] **Step 5: Build both + tests.**
Host: `distrobox enter droppix-dev -- bash -lc 'cmake --build /home/Spinjitsudoomyt/droppix-build -j 2>&1 | grep -iE "error|Built target" | tail; ctest --test-dir /home/Spinjitsudoomyt/droppix-build 2>&1 | tail -3'` (81/81 still pass).
Android: `:app:assembleDebug :app:testDebugUnitTest` → BUILD SUCCESSFUL.

- [ ] **Step 6: Commit** — `fix: address Part 1 review follow-ups (approve-request order, NSD main-thread, redundant save)`.

---

### Task 2: Wake datagram codec (host + Kotlin, byte-identical)

**Files:**
- Create: `host/src/wake.h`, `host/tests/test_wake.cpp` (+ add to `droppix_tests` in `host/CMakeLists.txt`)
- Create: `android/.../net/Wake.kt`, `android/.../test/.../net/WakeTest.kt`

**Interfaces:**
- Produces (host): `std::vector<unsigned char> encode_wake(uint16_t port);` and `bool decode_wake(const std::vector<unsigned char>& b, uint16_t& port);` (validates the `DPXW` magic + 6-byte length).
- Produces (Kotlin): `Wake.encode(port: Int): ByteArray` and `Wake.decode(b: ByteArray, len: Int): Int?` (returns the port or null).

- [ ] **Step 1: Host failing tests** (`test_wake.cpp`):

```cpp
#include "wake.h"
using namespace droppix;
TEST(Wake, RoundTrip) {
  auto b = encode_wake(27000); uint16_t p = 0;
  ASSERT_EQ(b.size(), 6u);
  EXPECT_EQ(b[0],'D'); EXPECT_EQ(b[1],'P'); EXPECT_EQ(b[2],'X'); EXPECT_EQ(b[3],'W');
  EXPECT_EQ(b[4], 27000>>8); EXPECT_EQ(b[5], 27000&0xFF);
  ASSERT_TRUE(decode_wake(b, p)); EXPECT_EQ(p, 27000);
}
TEST(Wake, RejectsBadMagicOrLen) {
  uint16_t p;
  EXPECT_FALSE(decode_wake({'X','X','X','X',0,0}, p));
  EXPECT_FALSE(decode_wake({'D','P','X','W',0}, p));   // too short
}
```

- [ ] **Step 2: Host impl** (`wake.h`):

```cpp
#pragma once
#include <vector>
#include <cstdint>
namespace droppix {
inline std::vector<unsigned char> encode_wake(uint16_t port) {
  return {'D','P','X','W', (unsigned char)(port>>8), (unsigned char)(port&0xFF)};
}
inline bool decode_wake(const std::vector<unsigned char>& b, uint16_t& port) {
  if (b.size()!=6 || b[0]!='D'||b[1]!='P'||b[2]!='X'||b[3]!='W') return false;
  port = (uint16_t(b[4])<<8) | b[5]; return true;
}
}  // namespace droppix
```

Add `test_wake.cpp` to `droppix_tests` in CMake; reconfigure + run `ctest -R Wake`.

- [ ] **Step 3: Kotlin failing test** (`WakeTest.kt`) asserting the same 6 bytes for port 27000 and a decode round-trip; bad-magic → null.

- [ ] **Step 4: Kotlin impl** (`Wake.kt`): `encode` returns `byteArrayOf('D','P','X','W', (port ushr 8), port)` (as bytes); `decode(b, len)` checks `len==6` + magic, returns `((b[4] and 0xFF) shl 8) or (b[5] and 0xFF)` else null.

- [ ] **Step 5: Run both test suites** → green.

- [ ] **Step 6: Commit** — `feat(wake): UDP wake datagram codec (host + android)`.

---

### Task 3: Host avahi-browse parser for tablets

**Files:**
- Create: `host/src/mdns_browse.h`, `host/src/mdns_browse.cpp`, `host/tests/test_mdns_browse.cpp` (+ CMake: add the .cpp to `droppix_core` sources and the test to `droppix_tests`)

**Interfaces:**
- Produces: `struct MdnsDevice { std::string name, address; uint16_t port; };` and
  `std::vector<MdnsDevice> parse_avahi_browse(const std::string& text);` — parses the resolved (`=`) lines
  of `avahi-browse -rptk` (fields split by `;`: `=;iface;proto;name;type;domain;hostname;address;port;txt`),
  IPv4 only, dedup by name (last wins).

- [ ] **Step 1: Failing test** (`test_mdns_browse.cpp`):

```cpp
#include "mdns_browse.h"
using namespace droppix;
static const char* kSample =
  "+;eth0;IPv4;Nexus 10;_droppix-client._tcp;local\n"
  "=;eth0;IPv4;Nexus 10;_droppix-client._tcp;local;nexus.local;192.168.1.42;48000;\"\"\n";
TEST(MdnsBrowse, ParsesResolvedLine) {
  auto v = parse_avahi_browse(kSample);
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0].name, "Nexus 10");
  EXPECT_EQ(v[0].address, "192.168.1.42");
  EXPECT_EQ(v[0].port, 48000);
}
TEST(MdnsBrowse, IgnoresNonResolvedAndIPv6) {
  auto v = parse_avahi_browse("=;eth0;IPv6;X;_droppix-client._tcp;local;h;fe80::1;48000;\"\"\n");
  EXPECT_TRUE(v.empty());
}
```

- [ ] **Step 2: Run, verify fail** (after CMake add + reconfigure).

- [ ] **Step 3: Implement** `parse_avahi_browse` — split the text into lines; for each line starting
with `=` whose 3rd field is `IPv4`, split on `;`, take field[3]=name, field[7]=address, field[8]=port
(atoi); collect, dedup by name keeping the last. The device name itself may contain a `;`? avahi
escapes it as `\;`; for our own service names (Build.MODEL) this is rare — split on `;` and accept the
simple case (note the limitation in a comment).

- [ ] **Step 4: Run, verify pass** — `ctest -R MdnsBrowse` → PASS; full `ctest` green.

- [ ] **Step 5: Commit** — `feat(host): avahi-browse parser for _droppix-client._tcp tablets`.

---

### Task 4: Host GUI — Devices panel + wake send

**Files:**
- Create: `host/gui/mdns_browser.h`, `host/gui/mdns_browser.cpp` (+ CMake to `droppix_gui`)
- Modify: `host/gui/main_window.{h,cpp}`

**Interfaces:**
- Consumes: `parse_avahi_browse`, `encode_wake`.
- Produces: `class MdnsBrowser : public QObject { void start(); void stop(); signals: void devicesChanged(QList<MdnsDevice>); };` — runs `avahi-browse -rptk _droppix-client._tcp` on a `QTimer` (every 3 s), parses, emits the list. `available()` via `QStandardPaths::findExecutable("avahi-browse")`.

- [ ] **Step 1: MdnsBrowser** — on a 3 s `QTimer`, launch `avahi-browse -rptk _droppix-client._tcp` via
`QProcess` (read all output on finish), `parse_avahi_browse(output)`, `emit devicesChanged(list)`.
`start()` begins the timer + does one immediate run; `stop()` stops the timer + kills any in-flight process.

- [ ] **Step 2: MainWindow Devices panel.** Add a `QGroupBox "Devices on network"` containing a
`QListWidget devicesList_` and a `QPushButton "Connect"` (styled like the existing buttons). Own an
`MdnsBrowser browser_`; `start()` it in the constructor (only if `available()`), `stop()` in `closeEvent`.
On `devicesChanged`, repopulate `devicesList_` (store each row's `address`+`port` via
`QListWidgetItem::setData`). Hide the panel if `avahi-browse` is unavailable.

- [ ] **Step 3: Connect → start + wake (and pre-authorize the dial-back).** On the Connect button (or
double-click a row): if `!controller_.running()`, call the existing start path (`onStartStop()`); read
the selected item's address+port; record the woken IP for auto-approval (so the tablet's dial-back isn't
gated by a second dialog — the PC already chose it), then send the wake via a `QUdpSocket`:

```cpp
auto bytes = encode_wake((uint16_t)collectSettings().port);  // PC stream port the tablet will dial
QByteArray dg(reinterpret_cast<const char*>(bytes.data()), (int)bytes.size());
pendingWakes_[addr] = QDateTime::currentMSecsSinceEpoch();   // member: QHash<QString,qint64>
QUdpSocket sock;
sock.writeDatagram(dg, QHostAddress(addr), (quint16)wakePort);
```

- [ ] **Step 4: Auto-approve the woken tablet.** Extend the `approvalRequested` handler set up in Part 1
(in the MainWindow constructor) so that BEFORE the remembered-id check / dialog it auto-approves a
just-woken peer:

```cpp
// at the top of the approvalRequested(id, name, ip) lambda:
const qint64 woken = pendingWakes_.value(ip, 0);
if (woken && QDateTime::currentMSecsSinceEpoch() - woken < 15000) {
    pendingWakes_.remove(ip);
    const QString key = id.isEmpty() ? ip : id;
    approved_.approve(key);                 // remember it too
    controller_.writeLine("approve " + key);
    return;
}
```

- [ ] **Step 6: Build droppix_gui clean + full ctest green.** Sanity: with a fake advertiser running
(`avahi-publish-service "FakeTab" _droppix-client._tcp 48000 &`), confirm `avahi-browse -rptk
_droppix-client._tcp` lists it (proves the browse command/args); the live panel is verified by the human.

- [ ] **Step 7: Commit** — `feat(gui): Devices panel browses tablets + sends UDP wake (auto-approve woken peer)`.

---

### Task 5: Android — advertise `_droppix-client._tcp` + wake listener + confirm

**Files:**
- Create: `android/.../net/WakeService.kt`
- Modify: `android/.../ui/ConnectActivity.kt`

**Interfaces:**
- Consumes: `Wake.decode`, `NsdManager`, `DeviceIdentity.displayName`, `ConnectActivity.connectTo`.
- Produces: `class WakeService(ctx) { fun start(onWake: (host: String, port: Int) -> Unit); fun stop() }`
  — binds a `DatagramSocket` (ephemeral port), registers `_droppix-client._tcp` via NSD with that port +
  the device name, and runs a receive loop; on a valid `DPXW` datagram, invokes `onWake(sender.hostAddress,
  decodedPort)` on the main thread. `stop()` unregisters NSD + closes the socket + ends the loop.

- [ ] **Step 1: WakeService.** Bind `DatagramSocket(0)` (ephemeral), read `localPort`. Register NSD
service: `NsdServiceInfo` with `serviceType="_droppix-client._tcp"`, `serviceName=DeviceIdentity.displayName(ctx)`,
`port=localPort`; `nsdManager.registerService(info, PROTOCOL_DNS_SD, regListener)`. Start a background
thread looping `socket.receive(packet)`; for each packet call `Wake.decode(packet.data, packet.length)` —
on non-null port, post `onWake(packet.address.hostAddress, port)` to the main thread. `stop()`:
`unregisterService`, `socket.close()` (unblocks receive), join the thread; guard double-stop.

- [ ] **Step 2: ConnectActivity wiring.** Own a `WakeService`. In `onResume` (alongside the existing
Discovery start): `wakeService.start { host, port -> showConnectConfirm(host, port) }`. In `onPause`:
`wakeService.stop()`. `showConnectConfirm` shows an `AlertDialog` "Connect to `<host>`?" with Connect/Cancel;
Connect → `connectTo(host, port)`. Keep all UI on the main thread.

- [ ] **Step 3: Build** — `:app:assembleDebug :app:testDebugUnitTest` → BUILD SUCCESSFUL.

- [ ] **Step 4: Commit** — `feat(android): advertise _droppix-client._tcp + UDP wake listener + confirm`.

---

## Self-Review

- **Spec coverage (Part 2):** tablet advertises `_droppix-client._tcp` + wake port (T5); host browses via
  `avahi-browse` + Devices panel (T3,T4); PC Connect → start + UDP wake (T4); wake datagram codec both
  ends (T2); tablet wake listener + "Connect to <PC>?" confirm → dial back (T5). Streaming direction
  unchanged. A wake-initiated tablet connection is NOT localhost, so Part 1's approve gate would
  normally fire — but T4 records the woken tablet's IP and the `approvalRequested` handler auto-approves
  a connection from a just-woken IP (within 15 s) without a dialog, matching the spec's "PC-initiated =
  auto-approved." (An unsolicited LAN connection from a non-woken device still gets the dialog.) The 3
  Part-1 follow-ups are T1.
- **Placeholder scan:** none — codecs/parsers have full code + tests; integration tasks give exact files,
  commands, and human-verification notes where unit tests don't apply.
- **Type consistency:** `encode_wake`/`decode_wake` (T2) used by T4; `parse_avahi_browse`/`MdnsDevice`
  (T3) used by T4's `MdnsBrowser`; `Wake.decode` (T2) used by T5; `connectTo` (Part 1) used by T5.

## Execution

Subagent-driven, T1→T5. After T5, final whole-branch review, then merge to master.
