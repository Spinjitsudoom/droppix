# droppix

Use an Android tablet as a **true extended monitor** (not a mirror) for a Linux/Wayland PC — plus touch, stylus pressure, auto-rotation, and audio out to the tablet. A Spacedesk-like setup for KDE Plasma.

> Status: early but working. Verified end-to-end on a Nexus 10 (Android 5.1.1 / API 22) driving a Plasma 6 / KWin desktop over both USB and WiFi.

## What it does

- **Extended display** — a virtual monitor (via [`evdi`](https://github.com/DisplayLink/evdi)) is captured, H.264-encoded (libx264; VAAPI planned), and streamed to the tablet, which decodes with hardware MediaCodec.
- **Touch + stylus** — finger touches drive an absolute cursor on the droppix monitor via a uinput touchscreen bound to that output; pen pressure is forwarded as `ABS_PRESSURE`.
- **Auto-rotation** — the tablet reports its physical orientation; the host re-streams portrait/landscape-shaped pixels and the tablet rotates naturally.
- **Audio sink** — route any app's audio to a dedicated PipeWire sink and it plays on the tablet (handy for e.g. Wii U gamepad audio in Cemu).
- **Transports** — USB (`adb reverse`) or WiFi (mDNS discovery + 6-digit PIN pairing over TLS).

## Requirements (host)

droppix is deeply integrated with the host, so some things can't ship inside the AppImage and must be present on the machine:

- **KDE Plasma 6 / KWin** — the GUI drives `kscreen-doctor` and `qdbus` to place/rotate the virtual output. (This also guarantees a Qt6 runtime, which is why the AppImage doesn't bundle Qt.)
- **The `evdi` kernel module** — install via your distro / DKMS (a kernel module can't live in an AppImage). Required for the extended-monitor feature; USB/WiFi test-pattern works without it.
- **polkit / `pkexec`** — the streamer runs as root for uinput + evdi.
- **PipeWire** (`parec`) — for the audio-to-tablet sink.
- **avahi-daemon** — for WiFi discovery (`avahi-publish`/`avahi-browse`).
- **`adb`** — for USB connections.

## Layout

| Path | What |
|------|------|
| `host/` | C++ streaming engine (`droppix_stream`) + Qt6 control-panel GUI (`droppix_gui`) |
| `android/` | Native Kotlin client (MediaCodec decode, touch/orientation/audio back-channel) |
| `packaging/` | AppImage build script |
| `docs/` | Design specs + phased implementation plans |
| `macos/` | Archived experimental macOS backend (not wired into the build) |

## Building

**Host** (needs Qt6, OpenSSL, libx264, evdi, a C++17 toolchain + CMake):

```bash
cmake -S host -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
```

`droppix_stream` is the engine; `droppix_gui` supervises it. The evdi path needs root for uinput/virtual-display, so the GUI launches the engine via `pkexec`.

**AppImage** — bundles both binaries (`droppix_gui` + `droppix_stream`) and the codec/streaming libs (ffmpeg, x264, OpenSSL, libevdi), using the host's Qt6 (see [Requirements](#requirements-host)). On launch the streamer is relocated to `~/.local/share/droppix/runtime/` so the root (evdi) path works despite the AppImage's read-only mount.

```bash
bash packaging/appimage/build-appimage.sh   # run on the host; builds the binaries in the distrobox first
```

**Flatpak** — full-function on the KDE runtime (`org.kde.Platform//6.10`, which provides Qt6). Builds x264 + ffmpeg-with-x264 + libevdi + droppix as modules. Because a sandbox can't run droppix's root/evdi streamer or reach host KWin/PipeWire/adb, the app reaches the host via `flatpak-spawn --host` (which effectively disables the sandbox — the AppImage delivers the same thing). Same host [Requirements](#requirements-host) apply.

```bash
bash packaging/flatpak/build-flatpak.sh      # installs to --user + drops a .flatpak in "complete builds/"
flatpak run org.droppix.Droppix
```

**Android** (Android SDK + JDK 17; minSdk 21):

```bash
cd android && ./gradlew assembleDebug
```

## Wire protocol

Length-prefixed framing (`[u32 BE len][u8 type][body]`); the host (C++) and app (Kotlin) codecs are byte-identical and locked by shared test vectors. SPS/PPS travel in-band ahead of each IDR. See `docs/superpowers/specs/` for the full spec.

## License

MIT — see [LICENSE](LICENSE).
