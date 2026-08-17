# droppix desktop client

Qt6 Linux **receive** client for the droppix wire protocol. Decode-only: no `evdi`, uinput, or host desktop-backend logic.

## Role

Connect to a running `droppix_stream` / `droppix_gui` session (WiFi / TLS PIN, or other TCP endpoint the host exposes) and display the H.264 stream with local settings (quality, flip, brightness/contrast, overlay).

Shares `host/src/protocol.cpp` by relative include so the wire codec stays identical to the host and Android app.

## Look

The UI is deliberately a sibling of the host GUI, not a lookalike: it includes the host's
`gui/style.h` + `theme.h` **directly** (header-only, no linking) rather than copying the
palette. The copy it used to keep had already drifted — the client painted its window in the
host's *surface* colour instead of its background.

- `gui/header_bar.*` — logo, title, live status dot, one primary action (Connect ⇄
  Disconnect), Settings, theme toggle. Stays visible while streaming: Disconnect lives there
  and there is no menu bar to fall back on.
- `gui/idle_page.*` — the "not connected" card. The window used to be an empty black widget
  when idle, which reads as broken rather than ready.
- `gui/client_theme.*` — dark/light preference in `QSettings`, matching how the client stores
  everything else (the host's file-based `theme_pref` is its own convention, not shared).

## Build

```bash
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
```

Needs Qt6 (Widgets, Network, Multimedia), OpenSSL, FFmpeg.

Build **off** the repo mount: it is CIFS and has no exec permission, so in-tree binaries will
not run.

```bash
distrobox enter droppix-dev -- bash -lc '
  cmake -S "<repo>/client" -B ~/droppix-client-build && cmake --build ~/droppix-client-build -j8'
~/droppix-client-build/droppix_client
```

`droppix_client` takes no arguments — hosts are chosen in the Connect dialog.

Packaging: `packaging/appimage/build-client-appimage.sh`, `packaging/flatpak/build-client-flatpak.sh`.

## Layout

| Path | What |
|------|------|
| `src/` | Transport, TLS trust, video decode, audio play, settings |
| `gui/` | Connect UI, video widget, settings dialog |
| `tests/` | Protocol / client unit tests |
