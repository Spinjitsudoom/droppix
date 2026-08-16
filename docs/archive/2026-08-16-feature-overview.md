> **Archived snapshot — 2026-08-16. Not a living document.**
>
> This is a point-in-time overview written in one pass. It is kept for its wide-angle
> narrative (glossary, quick start, end-to-end walkthroughs) and is **not** updated with
> the code. Where it disagrees with the living docs, the living docs win:
>
> | For | Read |
> |---|---|
> | Is a feature shipped? | [`docs/STATUS.md`](../STATUS.md) |
> | How the system fits together | [`docs/ARCHITECTURE.md`](../ARCHITECTURE.md) |
> | Wire protocol | [`docs/WIRE.md`](../WIRE.md) |
> | Build / requirements | [`README.md`](../../README.md) |
> | Known traps | [`docs/lessons/INDEX.md`](../lessons/INDEX.md) |

---

# Spacedesk for Linux — Complete Feature Documentation

**Project Name:** droppix  
**Status:** Active development  
**Target:** Turn an Android tablet into a true extended monitor for a Linux PC

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Core Features](#core-features)
3. [Transport & Protocol](#transport--protocol)
4. [Security & Pairing](#security--pairing)
5. [Client Settings](#client-settings)
6. [Advanced Features](#advanced-features)
7. [Build & Deployment](#build--deployment)

---

## Architecture Overview

### System Design

Spacedesk for Linux is a client-server streaming system that turns an Android tablet into a true extended monitor (not a mirror) for a Linux PC. The architecture consists of three main components:

```
┌──────────────── LINUX HOST (C++ daemon) ────────────────┐
│ [evdi kernel module] → VirtualDisplay (libevdi)         │
│         ▼                                                │
│ Capturer (dirty-rect framebuffer) → Encoder (VAAPI)     │
│         ▼                                                │
│ TransportServer (TCP/TLS)  ◄ USB (AOA) / WiFi (mDNS)   │
│     ▲ video ↓ / input ↑                                 │
│ InputInjector (uinput: touch+pen+pressure)              │
└────────────────────────────────────────────────────────┘
         │  USB cable (AOA) / WiFi
┌────────┼────────── ANDROID APP (Kotlin) ──────────────┐
│        ▼                                               │
│ TransportClient → Decoder (MediaCodec) → SurfaceView   │
│ InputCapture → encodes touch/stylus → sends upstream   │
└─────────────────────────────────────────────────────────┘
```

### Target Environment

- **OS:** Linux with Wayland (Bazzite/Fedora, KDE Plasma)
- **Compositor:** KWin 6.6.5+ (Wayland session)
- **Virtual Display:** `evdi` kernel module (DisplayLink's Extensible Virtual Display Interface)
- **GPU:** Required for VAAPI hardware video encoding
- **Android:** 3.1+ for AOA support; tested on Nexus 10 (Android 5.1.1)

---

## Core Features

### 1. **Extended Display (Virtual Monitor)**

**What it does:** Creates a new virtual monitor that appears as a connected display to the Linux desktop. The tablet shows this virtual display full-screen, acting as a true extended monitor.

**How it works:**
- Host loads the `evdi` kernel module to create a fake HDMI monitor
- KWin Wayland compositor detects the new output and extends the desktop onto it
- The host's `Capturer` reads the framebuffer using libevdi's dirty-rectangle optimization
- Changes are sent to the tablet in real-time

**Why it matters:** Unlike mirroring, you can use the tablet as a separate workspace — run an app on the tablet while your main monitor shows something else, or extend your desktop across both displays.

---

### 2. **Video Streaming (VAAPI H.264)**

**What it does:** Captures the virtual monitor and encodes it in real-time, streaming the video to the Android app.

**How it works:**
- **Codec:** H.264 (via VAAPI hardware acceleration)
- **Frame rate:** Configurable (default 60 fps, client can request 30)
- **Quality:** Configurable bitrate (Low/Medium/High = 4000/8000/16000 kbps)
- **Latency:** Optimized for low latency:
  - No B-frames (only I-frames and P-frames)
  - Zero-latency rate control
  - Dirty-rectangle optimization (only changed regions encoded)
  - Repeat-headers: SPS/PPS sent in-band with every IDR frame

**Video quality negotiation:**
- Client requests quality in HELLO handshake
- Host uses client's preference if available, falls back to host default
- Both clients (Linux and Android) can change quality mid-stream (triggers reconnect)

---

### 3. **Audio Output**

**What it does:** Streams audio from a selected source on the PC to the tablet, so an app's audio (e.g., emulator gamepad audio) plays on the tablet while PC speakers keep their own audio.

**How it works:**

- **Audio format:** Fixed at 48 kHz, stereo, 16-bit signed little-endian (s16le)
- **Capture:** Host creates a virtual PipeWire sink named `droppix-audio`
- **User routes:** Applications that should output to the tablet are routed to the `droppix-audio` sink
- **Streaming:** The `AudioStreamer` runs `pw-record` in the user session to capture the sink's monitor as raw PCM
- **Transport:** Audio chunks are framed as `AUDIO` messages on the same TLS connection as video
- **Playback:** Android `AudioTrack` plays the PCM stream on the tablet

**Features:**
- **Best-effort:** Audio never blocks video/touch — if capture fails, the stream continues video-only
- **Single session:** Only one connection can capture audio at a time (enforced by advisory file locking)
- **Low latency:** ~50–80 ms end-to-end (adequate for emulation; not frame-locked A/V sync)

**Client control:** Clients can enable/disable audio per connection in their settings.

---

### 4. **Touch Input**

**What it does:** Finger touches on the tablet act as mouse pointer movements and clicks on the virtual monitor.

**How it works:**

- **Android side:**
  - Touch events on the `SurfaceView` are captured
  - Coordinates are normalized to 0–65535 (device-independent)
  - Sent as `INPUT` messages to the host (action: down/move/up)

- **Host side:**
  - `InputInjector` creates a `uinput` virtual absolute pointer device
  - Touch coordinates are mapped from the tablet's normalized space to the virtual monitor's desktop geometry
  - Mouse movements and clicks are injected into the X11/Wayland input stack

**Features:**
- **Single-pointer only** in v1 (multi-touch gestures deferred)
- **Absolute positioning:** Touch position directly maps to that spot on the desktop
- **Pressure support:** Touch pressure (0–1023) is also sent
- **Requires root:** Touch injection needs `/dev/uinput` access (the streamer already runs as root via `pkexec`)

---

### 5. **Stylus Pressure (Pen Input)**

**What it does:** An Android stylus is recognized as a pen tool with pressure sensitivity, allowing pressure-aware apps (Krita, Photoshop) to detect pen pressure.

**How it works:**

- **Android:** Stylus events include pressure via `MotionEvent.getPressure()` and tool type
- **Wire format:** Sent in the same `INPUT` channel as touch (action, x, y, pressure)
- **Host mapping:** The `InputInjector` creates a separate `uinput` pen device with `ABS_PRESSURE` and tool-type flags (`BTN_TOOL_PEN`, `BTN_TOOL_FINGER`)
- **Desktop integration:** KWin/X11 sees this as a real graphics tablet, so apps like Krita recognize it natively

---

### 6. **Auto-Orientation**

**What it does:** Physically rotating the tablet automatically reorients the extended display — portrait workspace when held portrait, landscape when held landscape.

**How it works:**

- **Android:** The app runs with `screenOrientation="fullSensor"` so it auto-rotates based on device orientation
- **Orientation detection:** An `OrientationEventListener` + `OrientationMapper` detect physical rotation and send an `ORIENTATION` message (code 0/1/2/3 for 0°/90°/180°/270°)
- **Host behavior:** When the code crosses a portrait↔landscape boundary, the daemon **restarts the session** and rebuilds the virtual display at swapped dimensions (e.g., 1920×1080 → 1080×1920)
- **Reconnect:** The tablet's existing reconnect loop (~1 second) picks up the new stream and displays it at the new orientation
- **Touch:** Touch input mapping is recalculated for the new dimensions automatically

**Why restart?** Only the host (KWin) can reflow a workspace to portrait size. By making the display natively portrait-shaped, Android's auto-rotate orients the picture naturally without rotation math fighting between host and client.

**Rotation lock (Android only):** Clients can lock to the current orientation to prevent sensor-driven rotation during streaming.

---

### 7. **Multiple USB/WiFi Transports**

**What it does:** Supports streaming over multiple transport methods, automatically selecting the best available or letting users choose.

#### **A. USB via AOA (Android Open Accessory)**

- **Requirements:** USB cable; **no USB tethering**, **no Developer Options** needed
- **Handshake:** Host switches tablet into AOA mode (control requests 51/52/53), tablet re-enumerates as a USB accessory
- **Trust:** Physical cable is the trust boundary — plaintext, no TLS
- **Speed:** USB 2.0 (480 Mbps) easily carries the 8 Mbps default bitrate
- **Detection:** GUI enumerates via a udev rule; plug-and-play after "use by default"
- **Throughput:** Validated on Nexus 10 to support full video streaming

**Benefits:** No setup steps — just plug in the cable and stream. Works on tablets with no tethering support.

#### **B. USB via Tethering**

- **Requirements:** USB cable + USB tethering enabled on tablet + Developer Options with USB debugging
- **Setup:** Host connects via `adb reverse` tunnel (localhost over the USB link)
- **Trust:** Localhost (`127.0.0.1`) is auto-trusted; TLS still wraps it for consistency
- **Speed:** Limited by tethering bandwidth (typically slower than AOA)

#### **C. WiFi (mDNS + TLS + PIN Pairing)**

- **Discovery:** Host advertises via mDNS (`_droppix._tcp`)
- **Connection:** Tablet scans for available hosts; user picks one
- **Security:** TLS wraps the stream; first connect requires PIN pairing
- **PIN pairing:** 6-digit code (derived from host's certificate) shown on PC and entered on tablet to verify identity
- **Pinning:** After successful pairing, the tablet stores the certificate fingerprint and verifies it on subsequent connects
- **Reconnect:** Subsequent connects are silent (no PIN needed); if the host cert changes, the tablet warns and offers to re-pair

**Multi-transport benefit:** The same protocol runs over all three, so a single codebase supports USB (AOA), USB (tethering), and WiFi transparently.

---

### 8. **Client-Owned Display Settings**

**What it does:** Moves key display settings from the host GUI to the client (tablet or Linux monitor), allowing each device to choose its own preferences.

**Settings moved to client:**

1. **Resolution:** Client sends its desired resolution in HELLO
   - Android: Dropdown of presets, defaulting to device's native screen resolution
   - Linux: Manual picker with native default
   - Host builds a virtual display at that size

2. **Frame rate:** Client requests FPS in HELLO
   - Android/Linux: Dropdown (30 fps or 60 fps)
   - Default: 60 fps
   - Host encodes and sends at that rate

3. **Audio:** Client enables/disables audio per connection in HELLO
   - Toggle in client settings
   - Default: off
   - One audio session at a time (first to request it gets the stream)

4. **Rotation:** Already client-driven
   - Android: Sensor auto-rotate (live `ORIENTATION` messages)
   - Linux: Manual picker (0°/90°/180°/270°)
   - Host reflows to the chosen orientation

**Settings remaining on host GUI:**

- Source (test pattern vs. evdi virtual display)
- Bitrate/port (hidden, advanced)
- Refresh rate (hardware refresh, not frame rate)
- Touch input (enable/disable)
- Performance overlay (show/hide RTT/FPS HUD)
- Auto-connect (remember last device)

**Wire protocol:** Extended `HELLO` handshake (v4 and v5) carries the settings. An older client sends sentinels (0), and the host falls back to its own defaults.

**Immediate apply:** Changing settings mid-stream triggers a reconnect (~1 second) so the new HELLO takes effect.

---

### 9. **Quality / Bitrate Settings**

**What it does:** Lets clients choose the video encoding quality, affecting bandwidth and visual fidelity.

**Presets:**
- **Low:** 4000 kbps (good for WiFi, lower visual quality)
- **Medium:** 8000 kbps (default, good balance)
- **High:** 16000 kbps (best visual quality, requires good WiFi)

**How it works:**
- Client sends chosen bitrate in HELLO v5
- Host uses client value if available, falls back to its own `--bitrate` flag
- Encoder rate-control adjusts to target bitrate
- Both clients (Android and Linux) support this

**When to use:**
- Over slow WiFi or tethering: use Low
- Over fast WiFi/USB AOA: use High for sharper image

---

### 10. **Performance Overlay (Android)**

**What it does:** Shows real-time stats on the tablet display to help debug latency and quality issues.

**Displayed metrics:**
- **RTT (Round-Trip Time):** Network latency in milliseconds (measured via PING/PONG)
- **FPS:** Frames per second received and decoded
- **Decode latency:** Time spent decoding video frames
- **Video bitrate:** Current bitrate being streamed

**Toggle:** Android client has a "Performance overlay" switch in settings; overlay is shown/hidden based on this setting and any host `OVERLAY` messages.

---

## Transport & Protocol

### Wire Protocol (TCP)

**Frame format:** All messages use length-prefixed framing:

```
[u32 big-endian length][u8 type][payload]
```

where `length` covers the type byte and payload.

**Message types:**

| Type | Name | Direction | Purpose |
|------|------|-----------|---------|
| 1 | HELLO | client→host | Handshake: client sends screen size, FPS, audio request, rotation, bitrate |
| 2 | CONFIG | host→client | Server responds with negotiated resolution, FPS, codec info |
| 3 | VIDEO | host→client | H.264 encoded video frame |
| 4 | PING | both | Latency measurement + keep-alive |
| 5 | PONG | both | Echo to PING |
| 6 | BYE | host→client | Graceful disconnect |
| 7 | INPUT | client→host | Touch/stylus position and pressure |
| 8 | ORIENTATION | client→host | Tablet physical orientation (0/1/2/3) |
| 9 | AUDIO | host→client | Raw PCM audio data |
| 10 | OVERLAY | host→client | Show/hide performance overlay |
| 11 | TOUCH | client→host | Multi-touch contacts (v1: single pointer) |
| 12 | SCROLL | client→host | Scroll wheel (dx, dy, x, y) |
| 13 | MOUSEBUTTON | client→host | Mouse button (left/right/middle) |
| 14 | KEY | client→host | Keyboard input (keycode, action) |
| 15 | PEN | client→host | Stylus-specific events (x, y, pressure, flags) |

### Protocol Versions

| Version | Year | Key Changes |
|---------|------|-------------|
| 1 | — | Original: HELLO, CONFIG, VIDEO, PING, PONG, BYE |
| 2 | — | Added device name/id strings to HELLO |
| 3 | 2026-06 | Added orientation, device ID in HELLO |
| 4 | 2026-07 | **Client-owned settings:** FPS, audio, orientation moved to HELLO |
| 5 | 2026-07 | **Quality/bitrate:** Added bitrate_kbps to HELLO |

Back-compatibility: An older client (v3 or earlier) sends sentinels (0) for new fields; the host falls back to its own defaults. A v5 client talking to a v1 host is handled gracefully (host ignores unknown fields).

### HELLO Handshake (v5)

**Client sends:**
```
u32 version          // 5
u32 width            // screen width in pixels
u32 height           // screen height in pixels
u32 density          // screen density (dpi / 160)
u32 fps              // desired frames per second (30 or 60)
u8  audio_wanted     // 0 = no, 1 = yes
u8  orientation_code // 0/1/2/3 for 0°/90°/180°/270°
u32 bitrate_kbps     // desired video bitrate (e.g., 8000)
u16 name_len         // length of device name
... name bytes       // device name (e.g., "Nexus 10")
u16 id_len           // length of unique device ID
... id bytes         // unique ID (e.g., MAC address)
```

**Host responds:** Sends `CONFIG` with the negotiated resolution and codec info.

### TLS Encryption

**WiFi connections use TLS:**
- Host runs an OpenSSL server with a self-signed certificate
- Tablet client uses an `SSLSocket`
- First connection: PIN pairing validates the certificate
- Later connections: Certificate fingerprint is verified (pinned)

**USB connections (AOA) do not use TLS:**
- Physical cable is the trust boundary
- Plaintext protocol over bulk endpoints
- No PIN or pairing required

---

## Security & Pairing

### Trust Models

#### **USB (AOA) — Trust by Cable**
- Physical possession of the USB cable is the trust boundary
- No encryption, no authentication
- Fast and simple: plug in, it works
- Recommended for personal use on private networks

#### **WiFi — Trust by PIN Pairing**

**First connect:**
1. Tablet discovers host via mDNS
2. Tablet initiates TLS handshake
3. Host shows a 6-digit **pairing code** derived from its certificate
4. Tablet displays a dialog: "Enter the 6-digit code shown on the PC"
5. User types the code on the tablet
6. If match: Tablet pins the certificate's fingerprint (stored in SharedPreferences)
7. If mismatch: Connection rejected; user can retry or cancel

**Subsequent connects:**
1. Tablet initiates TLS handshake
2. Host sends its certificate
3. Tablet verifies the fingerprint matches the pinned value
4. If match: Connected (silent, no PIN)
5. If mismatch: Warns "PC identity changed — re-pair?" and offers to clear the pin

**Benefits:**
- Protects against man-in-the-middle (MITM) attacks
- Easy 6-digit entry (no complex pairing protocols)
- Asymmetric: Tablet verifies PC, PC verifies tablet (approve-on-host)

### Pairing Code Derivation

**Both ends compute identically:**
```
pairing_code = SHA-256(certificate_DER_bytes)
first_4_bytes = as_big_endian_u32(sha256[0:4])
code_mod = first_4_bytes % 1,000,000
formatted = zero_pad_left(code_mod, 6_digits)  // e.g., "012345"
```

Implemented in both C++ (`host/src/pairing_code.h`) and Kotlin (`android/net/PairingCode.kt`), with shared unit tests to ensure byte-identical output.

### Approval Gate (Host-Side)

**On WiFi connects from non-localhost peers:**
- Host displays an approval dialog: "Allow connection from Tablet ID X at IP Y?"
- User can click Allow or Deny
- If Deny: Connection is rejected
- If Allow: Device is remembered for future auto-approval

**USB (AOA) and localhost are always auto-trusted** (no dialog).

---

## Client Settings

### Android Client Settings

**Location:** Settings dialog on the main screen (shared preferences-backed)

**Available settings:**
- **Resolution:** Dropdown (presets), defaults to device's native screen size
- **Quality / Bitrate:** Low / Medium / High (4k / 8k / 16k kbps)
- **Frame rate:** 30 fps or 60 fps
- **Audio:** Toggle on/off
- **Rotation:** Auto (sensor-driven) or Locked (stays in current orientation)
- **Performance overlay:** Toggle to show/hide RTT/FPS/decode HUD

**Auto-reconnect:** Changing any setting while streaming triggers a reconnect (~1 second).

### Linux Client Settings

**Location:** Settings dialog (File → Settings)

**Available settings:**
- **Resolution:** Dropdown (presets), defaults to system's primary display resolution
- **Quality / Bitrate:** Low / Medium / High
- **Frame rate:** 30 fps or 60 fps
- **Audio:** Toggle on/off
- **Rotation:** Manual picker (0° / 90° / 180° / 270°)

**Storage:** QSettings-backed, persisted in `~/.config/droppix_client/`

### Host GUI Settings (Unchanged)

**Settings that remain on the host:**
- **Source:** Test pattern (display-only) or evdi virtual display (with input)
- **Refresh rate:** Physical display refresh (e.g., 60 Hz) — different from FPS
- **Bitrate / Port:** Hidden, only for CLI tuning
- **Touch input:** Enable/disable uinput injection
- **Performance overlay:** Enable/disable the RTT/FPS overlay on Android
- **Auto-connect:** Remember last connected device
- **Pairing code:** Shown in Settings; based on the host's TLS certificate

---

## Advanced Features

### 1. **Multi-Monitor Support**

**What it does:** Multiple tablets can connect simultaneously, each becoming a separate extended display.

**How it works:**
- Each tablet spawns a separate streamer process (evdi + encoder)
- Each streamer creates its own `droppix-touch` uinput device (named uniquely per session)
- Desktop layouts are managed via `kscreen-doctor` (extend/mirror/off)
- Touch injection binds each device to its corresponding output

**Coordination:**
- GUI runs one streamer per tablet
- A shared audio lock ensures only one session captures audio at a time

---

### 2. **Mirror Mode**

**What it does:** Mirrors the content of the primary display onto a tablet instead of extending.

**Usage:**
```bash
droppix_streamer --mirror
```

**How it works:**
- The evdi output is sized to match the primary display's resolution
- KWin's mirror layout presents identical content at matching size
- Touch injection maps to the primary display's location

**When to use:** Presentations, extended audience views, or when you want the tablet to show exactly what the main screen shows.

---

### 3. **Test Pattern Mode**

**What it does:** A display-only mode that doesn't require root or the evdi kernel module, useful for testing the UI without hardware.

**Usage:**
```bash
droppix_streamer --source test-pattern
```

**How it works:**
- No evdi kernel module, no `InputInjector`
- Streams a generated test pattern (gradient, timecode)
- No touch input (unprivileged process)
- Useful for prototyping and testing on systems without evdi support

---

### 4. **Dirty-Rectangle Optimization**

**What it does:** Only encodes the regions of the screen that changed, reducing CPU and bandwidth.

**How it works:**
- libevdi provides dirty rectangles (regions modified since last frame)
- Encoder only processes those regions
- Full-frame reference frames (keyframes) are still sent periodically
- Significantly reduces bandwidth on low-motion content (text editing, document viewing)

**Result:** Smooth streaming at lower bitrates compared to full-frame encoding.

---

### 5. **Low-Latency Encoder Tuning**

**Encoder configuration:**
- **No B-frames:** Only I-frames (keyframes) and P-frames, reducing encoding latency
- **Zero-latency rate control:** Immediate response to bitrate targets
- **Periodic keyframes:** IDR frames sent every ~2–5 seconds (configurable)
- **In-band headers:** SPS/PPS repeated with every IDR so clients can sync at any frame

**Result:** End-to-end latency of ~100–150 ms (good for interactive use, not frame-locked).

---

### 6. **Device Deduplication**

**What it does:** If the same tablet connects over multiple transports (e.g., WiFi and USB AOA simultaneously), it's recognized as one device.

**How it works:**
- Each device has a unique ID (sent in HELLO)
- The GUI deduplicates by ID, showing only one entry in the device list
- Later connections from a device with an existing ID update the same entry

**Benefit:** Prevents duplicate monitor entries when a tablet is connected over both WiFi and cable.

---

### 7. **Keyboard Input**

**What it does:** Sends keyboard events from the Android tablet to the host.

**Wire format:** `KEY` message (key code + action: down/up/repeat).

**Status:** Designed; implementation pending (T2 roadmap).

---

### 8. **Mouse Button Input**

**What it does:** Right-click (two-finger tap) and middle-click support.

**Implementation:**
- `MOUSEBUTTON` message carries button type and action
- Two-finger tap on tablet triggers right-click on host
- Coordinates are mapped relative to the virtual monitor's geometry

---

### 9. **Scroll Wheel Input**

**What it does:** Scroll gestures on the tablet translate to scroll events on the host.

**Wire format:** `SCROLL` message (dx, dy for direction + x, y for cursor position).

---

## Build & Deployment

### Build Requirements

**Host (Linux):**
- C++17 compiler (g++/clang)
- libevdi development headers
- FFmpeg + VAAPI libraries (`libva`, `libavcodec`, `libavutil`)
- OpenSSL (TLS support)
- Qt 6 (GUI)
- GoogleTest (unit tests)
- CMake 3.16+

**On immutable systems (Bazzite):**
- Use `distrobox`/`toolbox` to get compilers and dev libraries
- Runtime only needs: `evdi` kernel module + system GPU + Qt runtime

**Android:**
- Android Studio (or command-line `cmdline-tools` + Gradle)
- Android SDK 30+
- Kotlin 1.9+

### Build Commands

**Host:**
```bash
cd host
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
./droppix_streamer --help
```

**Android:**
```bash
cd android
./gradlew assembleDebug
adb install app/build/outputs/apk/debug/app-debug.apk
```

**Linux client:**
```bash
cd client
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
./droppix_client
```

### Distribution

**Host GUI (Linux):**
- **Flatpak:** Self-contained sandbox; dependencies packaged
- **AppImage:** Single-file binary (fat); pre-built for common distros
- **Package managers:** RPM (Fedora) and DEB (Debian) in preparation

**Android:**
- Distributed via Google Play Store (target: 2026 Q3)
- Sideload `.apk` for testing

---

## Roadmap & Planning

### Completed (Phase 1–3)

- ✅ Extended display via evdi + VAAPI H.264
- ✅ USB (AOA) transport + WiFi mDNS + TLS PIN pairing
- ✅ Touch input + stylus pressure
- ✅ Audio output to tablet
- ✅ Auto-orientation (portrait/landscape)
- ✅ Client-owned display settings
- ✅ Quality/bitrate settings
- ✅ Performance overlay

### In Progress / Planned (T1–T2)

- 🔄 AOA USB transport (spike validated; M1–M4 implementation)
- 🔄 Multi-monitor support (evdi backend refactor)
- ⏳ Keyboard input
- ⏳ Mouse input (right-click, middle-click)
- ⏳ Scroll wheel input
- ⏳ Rotation display (flipped) toggle
- ⏳ Cross-desktop portability (GNOME Mutter, etc.)

### Future (T2–T3)

- Color adjustments (brightness/contrast, GL render stage)
- Multi-touch gestures (pinch, zoom, rotate)
- Tablet → PC microphone / return audio
- Compressed audio (Opus) for WiFi bandwidth
- Frame-accurate A/V sync
- 120 fps streaming
- Multi-client display cloning

---

## Testing Strategy

### Unit Tests (GoogleTest / JUnit)

- **Protocol:** Codec round-trips, version compatibility, byte-identical between C++ and Kotlin
- **Math:** Coordinate mapping, orientation logic, pairing code derivation
- **Logic:** Session parameter selection, device deduplication, file locking

### Integration Tests

- **C++ daemon:** Transport + evdi + encoder stack on the dev machine
- **Android:** Protocol + MediaCodec decode + audio playback
- **Linux client:** Connect → settings → reconnect

### Manual E2E Tests

- **Smoke tests per phase:**
  - Phase 1: Virtual monitor appears; video streams; client can toggle overlay
  - Phase 2: Touch moves cursor; drag works; tap = click
  - Phase 3: Stylus pressure recognized in Krita
  - Phase 4: WiFi discovery + PIN pairing + auto-reconnect
  - Phase 5: Multi-monitor layout, mirror mode

- **Device coverage:**
  - Nexus 10 (Android 5.1.1, no tethering) — validates AOA
  - Modern tablets (Samsung Galaxy Tab, etc.)
  - Real-world latency measurement with PING/PONG + on-screen clock

---

## Glossary

| Term | Definition |
|------|-----------|
| **AOA** | Android Open Accessory; USB protocol allowing an Android device to be controlled by a USB host without needing tethering or Developer Options |
| **evdi** | Extensible Virtual Display Interface; a Linux kernel module that creates fake HDMI monitors, used by DisplayLink |
| **VAAPI** | Video Acceleration API; Intel/AMD GPU acceleration for video encoding/decoding |
| **H.264** | Video codec; widely supported, hardware-decodable on mobile, low latency |
| **TLS** | Transport Layer Security; encryption protocol wrapping the streaming TCP connection (WiFi only) |
| **mDNS** | Multicast DNS; local network discovery without a DNS server (finds `_droppix._tcp` services) |
| **PIN pairing** | First-time WiFi authentication: user enters a 6-digit code on the tablet to verify the PC's identity |
| **uinput** | Linux kernel input injection interface; used to send touch/stylus/mouse events as if from a real input device |
| **Dirty rectangles** | Changed regions of the framebuffer; libevdi provides these so the encoder only processes changed pixels |
| **Keyframe (IDR)** | H.264 I-frame; an independently decodable frame (no reference to prior frames); used to sync decoders |
| **SPS/PPS** | Sequence Parameter Set / Picture Parameter Set; H.264 headers; sent in-band before IDRs so decoders can configure themselves |

---

## Quick Start

### For Users

1. **Linux side:**
   - Install `droppix_gui` (via package manager or Flatpak)
   - Connect a USB cable or join the same WiFi as the tablet
   - Launch the GUI; it auto-discovers devices

2. **Android side:**
   - Install the `droppix` app (from Play Store or sideload)
   - Connect over USB (plug in cable) or WiFi (scan for PC)
   - First WiFi: Enter the 6-digit PIN shown on the PC
   - Enjoy your extended monitor!

3. **Adjust settings on the tablet:**
   - Settings → Resolution, FPS, Audio, Quality, Rotation, Overlay
   - Changes apply on next stream

### For Developers

1. **Clone the repo:** `git clone https://github.com/droppix/linux …`
2. **Build host:** `cd host && mkdir build && cmake .. && make`
3. **Build Android:** `cd android && ./gradlew assembleDebug && adb install …`
4. **Run tests:** `cd host/build && ctest`
5. **Start development:** Pick a task from the roadmap; branch from `master`, implement, test, PR.

---

## References

- **Wire protocol:** `/docs/superpowers/specs/2026-06-23-droppix-wire-protocol.md`
- **Architecture:** `/docs/superpowers/specs/2026-06-23-android-extended-display-design.md`
- **Audio:** `/docs/superpowers/specs/2026-06-29-audio-output-design.md`
- **TLS & pairing:** `/docs/superpowers/specs/2026-06-28-tls-pin-pairing-design.md`
- **AOA USB:** `/docs/superpowers/specs/2026-07-05-aoa-usb-transport-design.md`
- **Client settings:** `/docs/superpowers/specs/2026-07-10-client-owned-display-settings-design.md`
- **Quality & overlay:** `/docs/superpowers/specs/2026-07-11-quality-rotationlock-overlay-design.md`

---

**Last Updated:** 2026-07-18  
**Status:** Active development, Phase 1–3 complete, T1 features in progress
