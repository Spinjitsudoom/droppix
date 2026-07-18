# droppix

Use an Android tablet (or a second Linux machine) as a **true extended monitor** for a Linux PC — with touch, stylus pressure, keyboard, auto-rotation, mirror/extend, and audio out. A Spacedesk-like setup, built first for KDE Plasma / KWin, with an X11 backend and a desktop-agnostic seam for further ports.

> **Status (2026-07-18):** early but working end-to-end. Verified on a Nexus 10 (Android 5.1.1 / API 22) driving Plasma 6 / KWin over USB and WiFi. Hardware H.264 encode (NVENC → VAAPI → software x264), multi-monitor sessions, TLS PIN pairing, USB tether, AOA, stylus, and keyboard are on `master`. Living feature list: [`docs/STATUS.md`](docs/STATUS.md).

## What it does

- **Extended display** — a virtual monitor via [`evdi`](https://github.com/DisplayLink/evdi) is captured, H.264-encoded, and streamed to the client. Encode preference is **auto**: NVENC → VAAPI → libx264 (`--encoder auto|nvenc|vaapi|software`). Android decodes with hardware MediaCodec; the Linux desktop client uses FFmpeg.
- **Touch + stylus + mouse + keyboard** — absolute touch (incl. multi-touch and two-finger right-click), pen pressure / eraser, scroll / mouse buttons, and key events via uinput devices bound to the droppix output.
- **Mirror or extend** — per-session layout toggle on compositors that support it (KWin / X11 backends).
- **Auto-rotation** — the tablet reports orientation; the host re-streams portrait/landscape-shaped pixels.
- **Audio sink** — route app audio to a dedicated PipeWire sink; it plays on the tablet.
- **Transports** — USB (`adb reverse`), USB tethering, AOA accessory mode, or WiFi (mDNS discovery + 6-digit PIN over TLS).
- **Multi-monitor** — several tablets at once, each a native-resolution session managed by the host GUI.
- **Client-owned settings** — resolution / FPS / quality / audio / overlay / brightness / flip live in the client (HELLO v5).

## Requirements (host)

droppix is deeply integrated with the host, so some things can't ship inside the AppImage and must be present on the machine:

- **A supported desktop session** — primary path is **KDE Plasma 6 / KWin** (Wayland). An **X11** backend (`xrandr` / `xinput`) is also implemented. Other Wayland compositors (Sway, GNOME) are on the [cross-desktop roadmap](docs/superpowers/specs/2026-07-05-cross-desktop-portability-design.md); without a backend, the display may still appear but touch mapping is best-effort / Generic.
- **The `evdi` kernel module** — via distro / DKMS (cannot live in an AppImage). Required for the extended-monitor feature; test-pattern streaming works without it.
- **polkit / `pkexec`** — the streamer runs as root for uinput + evdi.
- **PipeWire** (`parec` / `pw-record`) — for the audio-to-tablet sink.
- **avahi-daemon** — for WiFi discovery (`avahi-publish` / `avahi-browse`).
- **`adb`** — for classic USB (`adb reverse`) connections.
- **GPU encode (optional)** — NVIDIA NVENC and/or VAAPI H.264 encode accelerate the stream; software x264 is always the fallback.

## Layout

| Path | What |
|------|------|
| `host/` | C++ streaming engine (`droppix_stream`) + Qt6 control-panel GUI (`droppix_gui`) |
| `android/` | Native Kotlin tablet client (MediaCodec decode, touch / stylus / keys / orientation / audio) |
| `client/` | Qt6 Linux **receive** client (same wire protocol; decode-only — no evdi) |
| `packaging/` | AppImage, Flatpak, and Android APK build scripts |
| `docs/` | [STATUS](docs/STATUS.md), [WIRE](docs/WIRE.md), design specs + plans |
| `macos/` | Archived experimental macOS backend (not wired into the build) |
| `spike/` | One-off validation spikes (e.g. AOA) |

## Building

**Host** (needs Qt6, OpenSSL, FFmpeg/libx264, evdi, libusb, a C++17 toolchain + CMake):

```bash
cmake -S host -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
```

`droppix_stream` is the engine; `droppix_gui` supervises it. The evdi path needs root for uinput/virtual-display, so the GUI launches the engine via `pkexec`. Force an encoder with `--encoder nvenc|vaapi|software` (default `auto`).

**Linux desktop client:**

```bash
cmake -S client -B build-client && cmake --build build-client -j
ctest --test-dir build-client --output-on-failure
```

**AppImage** — host bundles `droppix_gui` + `droppix_stream` and codec/streaming libs (ffmpeg, x264, OpenSSL, libevdi), using the host's Qt6. On launch the streamer is relocated to `~/.local/share/droppix/runtime/` so the root (evdi) path works despite the AppImage's read-only mount. A separate client AppImage script is also available.

```bash
bash packaging/appimage/build-appimage.sh          # host
bash packaging/appimage/build-client-appimage.sh   # desktop client
```

**Flatpak** — full-function on the KDE runtime (`org.kde.Platform//6.10`). Because a sandbox can't run droppix's root/evdi streamer or reach host KWin/PipeWire/adb, the app reaches the host via `flatpak-spawn --host` (effectively the same trust model as the AppImage). Same host [Requirements](#requirements-host) apply.

```bash
bash packaging/flatpak/build-flatpak.sh            # host → flatpak run org.droppix.Droppix
bash packaging/flatpak/build-client-flatpak.sh     # client
```

**Android** (Android SDK + JDK 17; minSdk 21):

```bash
cd android && ./gradlew assembleDebug
# or: bash packaging/android/build-apk.sh
```

## Wire protocol

Length-prefixed framing (`[u32 BE len][u8 type][body]`); host (C++), Android (Kotlin), and desktop client codecs stay byte-identical and are locked by shared test vectors. Current version is **HELLO v5** (types include Video, Touch, Pen, Key, Audio, Overlay, …). SPS/PPS travel in-band ahead of each IDR.

See [`docs/WIRE.md`](docs/WIRE.md) and `host/src/protocol.h`.

## Docs

| Doc | Purpose |
|-----|---------|
| [`docs/STATUS.md`](docs/STATUS.md) | Living feature / design status |
| [`docs/WIRE.md`](docs/WIRE.md) | Current protocol summary |
| [`docs/README.md`](docs/README.md) | Docs index |
| [`CLAUDE.md`](CLAUDE.md) | Agent entry; rules live under `.claude/rules/` (Cursor mirrors via symlink) |
| [`scripts/check-agent-sync.sh`](scripts/check-agent-sync.sh) | Verify Claude ↔ Cursor rules/skills stay symlinked |
| [`docs/superpowers/specs/`](docs/superpowers/specs/) | Historical design specs |
| [`docs/superpowers/plans/`](docs/superpowers/plans/) | Implementation plans |

## License

MIT — see [LICENSE](LICENSE).
