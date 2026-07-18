# Android as an Extended Display for Linux/Wayland — Design

**Date:** 2026-06-23
**Working name:** droppix
**Status:** Shipped on master (core product path).

## Goal

Build a Spacedesk-like system that turns an Android tablet into a **true extended
monitor** (not a mirror) for a Linux PC, primarily over **USB** (with WiFi also
supported), with **touch and stylus (pressure) input** sent back to the host so
the tablet works as a drawing surface. Lowest end-to-end latency is the priority.

## Target environment (verified on the dev machine)

- **OS:** Bazzite (Fedora 44 / Kinoite), immutable/atomic.
- **Compositor:** KDE Plasma 6.6.5, **KWin 6.6.5**, **Wayland** session.
- **Virtual display:** `evdi` kernel module **pre-installed** at
  `/lib/modules/<kver>/extra/evdi/evdi.ko.xz` (DisplayLink's Extensible Virtual
  Display Interface). `vkms` also available as a fallback. **No `rpm-ostree`
  layering required.**
- **Tooling:** `adb` installed. GPU present for VAAPI hardware encode.

Because the distro is immutable, all host build dependencies (compilers,
libevdi headers, ffmpeg/VAAPI dev libs, GoogleTest) are expected to live in a
`distrobox`/`toolbox` container or be otherwise available without modifying the
base image. The runtime only needs the already-present `evdi` module and the
system GPU/VAAPI stack.

## Key decisions (settled during brainstorming)

| Decision | Choice | Rationale |
|---|---|---|
| Display mode | Extend (new monitor) | The headline use case; mirror not wanted |
| Virtual display | `evdi` + `libevdi` direct capture | Already installed; KWin auto-extends onto it; gives framebuffer + dirty rects with lowest latency and no portal popups |
| Host daemon language | C/C++ | Most direct path to libevdi, VAAPI/ffmpeg, uinput |
| Video codec | H.264 via VAAPI hardware encode | Low latency, low CPU, universally decodable by Android MediaCodec |
| Android receiver | Native Kotlin app, MediaCodec hardware decode | Lowest latency; raw access to stylus pressure |
| Input back-channel | Touch + stylus with pressure | Drawing-surface use case |
| Transport | USB (`adb reverse` tunnel) **and** WiFi (mDNS + PIN pairing) from v1 | Both are the same TCP socket underneath |

## Rejected approaches

- **Capture via PipeWire/portal instead of libevdi:** adds a permission dialog
  per session, higher latency, full-frame capture (no cheap dirty-rects), and
  fiddly selection of the exact virtual output. More latency for less control.
- **KWin-native virtual output (no kernel module) via DBus:** the runtime API for
  arbitrary virtual outputs is immature/undocumented and KDE-version-specific;
  would likely break on updates. `evdi` is already installed and battle-tested.
  Kept only as a fallback if `evdi` proves unusable.

## Architecture

```
                        ┌──────────────── LINUX HOST (C++ daemon) ────────────────┐
 [evdi kernel module] → │  VirtualDisplay (libevdi)  →  Capturer (dirty-rect FB)  │
   creates fake monitor │         │ KWin extends desktop onto it                  │
   KWin treats as real  │         ▼                                               │
                        │  Encoder (VAAPI H.264, encodes changed regions)         │
                        │         ▼                                               │
                        │  TransportServer (TCP)  ◄ adb reverse (USB) / mDNS (WiFi)│
                        │         ▲ video down ↓ / input up ↑                      │
                        │  InputInjector (uinput virtual tablet: touch+pen+press) │
                        └──────────┼──────────────────────────────────────────────┘
                                   │  USB cable / WiFi
                        ┌──────────┼────────────── ANDROID APP (Kotlin) ──────────┐
                        │          ▼                                              │
                        │  TransportClient → Decoder (MediaCodec) → SurfaceView   │
                        │  InputCapture → encodes stylus/touch → sends upstream   │
                        └─────────────────────────────────────────────────────────┘
```

### Linux host daemon (C++) — one class per responsibility

| Component | Responsibility | Depends on |
|---|---|---|
| `VirtualDisplay` | Open evdi node, feed an EDID (resolution/refresh from the phone), present the monitor to KWin, manage connect/disconnect | `libevdi` |
| `Capturer` | Receive framebuffer + dirty rectangles from evdi; hand changed regions downstream | `VirtualDisplay` |
| `Encoder` | VAAPI H.264 encode of frames/regions; output a stream MediaCodec can decode; low-latency config (no B-frames, periodic keyframes) | `libva`/ffmpeg |
| `TransportServer` | Own the TCP socket; frame the protocol (video down, input up); manage one client session | sockets |
| `InputInjector` | Create a `uinput` virtual tablet; translate incoming events to absolute coords + pressure on the virtual monitor's geometry | `uinput` |
| `Daemon`/`main` | Wire components; config (resolution, fps, bitrate, codec); lifecycle; USB `adb reverse` setup; mDNS advertise | the above |

### Android app (Kotlin) — one class per responsibility

| Component | Responsibility |
|---|---|
| `TransportClient` | Connect over USB (localhost via adb tunnel) or WiFi (discovered host + PIN); read video, write input |
| `Decoder` | MediaCodec hardware H.264 decode directly onto a `Surface` |
| `DisplaySurfaceView` | `SurfaceView` showing decoded video full-screen |
| `InputCapture` | Capture `MotionEvent`s incl. stylus + `getPressure()`; serialize and send upstream |
| `ConnectionUi` | Pick USB vs WiFi, enter PIN, show status |

Each unit has one clear job and a narrow interface (e.g. `Capturer` emits
`Frame{data, rect}`; `Encoder` consumes frames and emits encoded packets), so
each can be built and tested independently.

## Wire protocol

Single TCP connection, length-prefixed typed messages both directions:

```
[ 4 bytes length ][ 1 byte type ][ payload ]
```

**Handshake (on connect):**
1. Phone → Host `HELLO` { protocol version, screen width/height/density, max decode capability }
2. (WiFi only) PIN pairing: host shows a code; phone sends `PAIR { pin }`; host validates. USB skips this (physical cable = trust).
3. Host → Phone `CONFIG` { negotiated resolution, fps, codec=H.264, SPS/PPS header }
4. Host creates the evdi monitor → KWin extends the desktop.

**Streaming:**
- Host → Phone `VIDEO` { pts, flags(keyframe), encoded H.264 access unit }
- Phone → Host `INPUT` { event batch — see below }
- Both directions `PING`/`PONG` for latency measurement + disconnect detection.
- Host → Phone `BYE` on shutdown; monitor torn down cleanly.

## Input & pressure mapping

- Android `MotionEvent` provides per-pointer `x, y, pressure, toolType
  (finger/stylus), action`. Sent as a compact batch:
  `{ tool, action, x_norm, y_norm, pressure_norm, pointerId }` with coordinates
  normalized 0..1 so resolution changes don't break it.
- On Linux, `InputInjector` creates a `uinput` **absolute** device. To behave as
  a tablet pen on *that monitor*, it maps `x_norm/y_norm` onto the virtual
  output's geometry (its position in the overall KWin layout) and emits
  `ABS_X/ABS_Y`, `ABS_PRESSURE`, `BTN_TOUCH`, `BTN_TOOL_PEN`/`BTN_TOOL_FINGER`.
  This is the same event vocabulary real graphics tablets use, so apps like Krita
  see real pressure.
- v1 targets single-pointer pen/finger. Multi-touch gestures deferred.

## Build phases (each independently runnable & testable)

- **Phase 0 — Spike:** Prove evdi works on this box: load module, create a
  monitor via libevdi, confirm KWin extends onto it and framebuffer data
  arrives. De-risks the riskiest assumption first.
- **Phase 1 — Display, USB-only:** Daemon evdi → capture → VAAPI H.264 → TCP;
  Android adb-tunnel connect → MediaCodec → SurfaceView. **Result: a working
  extended USB monitor, display-only.**
- **Phase 2 — Input:** `uinput` injector + Android touch capture → cursor/touch
  works on the virtual screen.
- **Phase 3 — Stylus pressure:** Pen tool type + pressure end-to-end (validate in
  Krita).
- **Phase 4 — WiFi:** mDNS discovery + PIN pairing; reuse the same socket/protocol.
- **Phase 5 — Polish:** config UI (resolution/fps/bitrate), reconnect handling,
  dirty-rect encode optimization, clean teardown.

## Testing strategy

- **C++ daemon:** unit-test pure logic (protocol framing/parsing,
  coordinate/pressure mapping math, EDID generation, config) with GoogleTest or
  Catch2. Hardware-touching parts (evdi, VAAPI, uinput) sit behind thin
  interfaces with fakes for unit tests, plus a small set of integration smoke
  tests run on the dev machine.
- **Android:** unit-test protocol serialization and event encoding (JUnit);
  instrumented/manual test for decode + display.
- **End-to-end:** a scripted smoke test per phase (e.g. Phase 1: bring up
  monitor, stream a known pattern, assert the phone receives decodable frames).
  TDD where logic is pure; manual verification for hardware/visual bits.

## Top risks & mitigations

1. **evdi behavior on this exact kernel/KWin** — mitigated by the Phase 0 spike
   before committing further.
2. **adb tunnel throughput** for high-res/fps — mitigated by H.264 + dirty-rects
   + configurable bitrate.
3. **End-to-end latency** (capture→encode→USB→decode→display) — measured via
   PING/PONG + on-screen latency overlay; encoder tuned for low latency (no
   B-frames, zero-latency rate control).
4. **WiFi security** (daemon exposed on LAN) — PIN pairing on first connect;
   USB path stays trust-by-cable.

## Development environment / prerequisites

- **Android app is developed on the Linux PC, not the tablet.** Kotlin is the
  source language; the build produces an `.apk` installed onto the tablet via
  `adb install`. Nothing called "Kotlin" is installed on the tablet.
- **Host dev tools:** Android Studio (Flatpak `com.google.AndroidStudio`) for the
  app, or command-line `cmdline-tools` + Gradle in a container; a C/C++ toolchain
  + `libevdi`, ffmpeg/VAAPI dev libs, and a test framework for the daemon
  (in a `distrobox`/`toolbox` since the base OS is immutable).
- **Tablet:** USB debugging enabled (Settings → Developer Options).

## Out of scope for v1

- Audio forwarding.
- Multi-touch gestures (pinch/zoom/rotate) on the host side.
- More than one simultaneous tablet/client.
- Windows/macOS hosts; non-KWin compositors (design leans on evdi which is
  compositor-agnostic, but only KWin/Wayland is targeted/tested for v1).
