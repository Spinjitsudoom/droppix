# droppix

Use an Android tablet as a **true extended monitor** (not a mirror) for a Linux/Wayland PC — plus touch, stylus pressure, auto-rotation, and audio out to the tablet. A Spacedesk-like setup for KDE Plasma.

> Status: early but working. Verified end-to-end on a Nexus 10 (Android 5.1.1 / API 22) driving a Plasma 6 / KWin desktop over both USB and WiFi.

## What it does

- **Extended display** — a virtual monitor (via [`evdi`](https://github.com/DisplayLink/evdi)) is captured, H.264-encoded (libx264; VAAPI planned), and streamed to the tablet, which decodes with hardware MediaCodec.
- **Touch + stylus** — finger touches drive an absolute cursor on the droppix monitor via a uinput touchscreen bound to that output; pen pressure is forwarded as `ABS_PRESSURE`.
- **Auto-rotation** — the tablet reports its physical orientation; the host re-streams portrait/landscape-shaped pixels and the tablet rotates naturally.
- **Audio sink** — route any app's audio to a dedicated PipeWire sink and it plays on the tablet (handy for e.g. Wii U gamepad audio in Cemu).
- **Transports** — USB (`adb reverse`) or WiFi (mDNS discovery + 6-digit PIN pairing over TLS).

## Layout

| Path | What |
|------|------|
| `host/` | C++ streaming engine (`droppix_stream`) + Qt6 control-panel GUI (`droppix_gui`) |
| `android/` | Native Kotlin client (MediaCodec decode, touch/orientation/audio back-channel) |
| `packaging/` | Thin AppImage build script |
| `docs/` | Design specs + phased implementation plans |
| `macos/` | Archived experimental macOS backend (not wired into the build) |

## Building

**Host** (needs Qt6, OpenSSL, libx264, evdi, a C++17 toolchain + CMake):

```bash
cmake -S host -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
```

`droppix_stream` is the engine; `droppix_gui` supervises it. The evdi path needs root for uinput/virtual-display, so the GUI launches the engine via `pkexec`.

**AppImage** (thin — relies on the host's Qt6 runtime):

```bash
bash packaging/appimage/build-appimage.sh
```

**Android** (Android SDK + JDK 17; minSdk 21):

```bash
cd android && ./gradlew assembleDebug
```

## Wire protocol

Length-prefixed framing (`[u32 BE len][u8 type][body]`); the host (C++) and app (Kotlin) codecs are byte-identical and locked by shared test vectors. SPS/PPS travel in-band ahead of each IDR. See `docs/superpowers/specs/` for the full spec.

## License

MIT — see [LICENSE](LICENSE).
