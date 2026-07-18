# droppix desktop client

Qt6 Linux **receive** client for the droppix wire protocol. Decode-only: no `evdi`, uinput, or host desktop-backend logic.

## Role

Connect to a running `droppix_stream` / `droppix_gui` session (WiFi / TLS PIN, or other TCP endpoint the host exposes) and display the H.264 stream with local settings (quality, flip, brightness/contrast, overlay).

Shares `host/src/protocol.cpp` by relative include so the wire codec stays identical to the host and Android app.

## Build

```bash
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
```

Needs Qt6 (Widgets, Network, Multimedia), OpenSSL, FFmpeg.

Packaging: `packaging/appimage/build-client-appimage.sh`, `packaging/flatpak/build-client-flatpak.sh`.

## Layout

| Path | What |
|------|------|
| `src/` | Transport, TLS trust, video decode, audio play, settings |
| `gui/` | Connect UI, video widget, settings dialog |
| `tests/` | Protocol / client unit tests |
