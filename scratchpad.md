# droppix — scratchpad

## Project identity

Spacedesk-like system: turn an Android tablet (or Linux desktop client) into a **true extended monitor** for a Linux PC (KDE Plasma / KWin first; X11 backend also shipped), with touch, stylus, keyboard, orientation, mirror/extend, and audio out. Verified E2E on Nexus 10 (Android 5.1.1) + Plasma 6 over USB and WiFi. Optional **host-served web PWA** client on branch `feat/web-pwa-client`.

License: MIT

## Git / collaboration (local-first)

| Remote | URL | Role |
|---|---|---|
| `origin` | `https://github.com/davidcarma/droppix.git` | **Our fork** - push here |
| `upstream` | `git@github.com:Spinjitsudoom/droppix.git` | Friend's repo - fetch only (`push` disabled) |

- Work locally; open PRs from `davidcarma` → `Spinjitsudoom`.
- When he catches up: `git fetch upstream && git merge upstream/master` then `git push origin master`.

## Current state (as of 2026-07-18)

- **Branch:** `feat/web-pwa-client` implements host-served HTTPS/WSS PWA (Partial; LAN E2E pending).
- **Host:** C++17 `droppix_stream` + Qt6 `droppix_gui`; evdi; AutoEncoder; uinput; PipeWire; DesktopBackend; `--web` / `--web-root` + `WebFrontend` / `WsChannel`.
- **Android client:** Kotlin, MediaCodec; HELLO v5.
- **Desktop client:** Qt6 Linux receive client in `client/`.
- **Web client:** `web/` TypeScript → `web/dist` (WebCodecs, AudioWorklet, input, fullscreen, PWA).
- **Packaging:** AppImage + Flatpak stage `web/dist` into `share/droppix/web` / runtime tarball.
- **Docs:** ARCHITECTURE, STATUS, WIRE (incl. WSS binding), lessons/.

## Key decisions (do not re-debate)

| Decision | Choice | Why |
|---|---|---|
| Display mode | Extend (evdi virtual monitor); mirror optional | Headline use case |
| Capture | libevdi direct | No portal popup; dirty rects |
| Host language | C++ | Direct libevdi / VAAPI / uinput |
| Codec | H.264, in-band SPS/PPS on every IDR | Universal decode |
| Transport | Same TCP framing; WSS binding for web | One protocol, many carriers |
| Web hosting | Host-served HTTPS on session port | Same-origin WSS; no CDN |
| Web port | Same session port; HTTP sniff vs native | Avoid port allocator clash |
| Packaging reality | Needs host `evdi`, polkit, PipeWire, avahi, adb | Kernel + root uinput |

## Architecture (at a glance)

```
evdi → Capturer → Encoder → TransportServer
                      ↑              ↕ wire v5 (TCP or WSS)
              InputInjector     Android / desktop / web PWA
```

## File map

| Path | Role |
|---|---|
| `host/src/` | Core + `web_frontend`, `ws_channel`, `web_root` |
| `host/gui/` | Qt6 GUI + URL/QR (`web_url`) |
| `android/` | Kotlin tablet client |
| `client/` | Qt6 Linux receive client |
| `web/` | Host-served PWA shell + WSS client |
| `packaging/` | AppImage, Flatpak, APK |
| `docs/` | STATUS, WIRE, ARCHITECTURE, specs |

## Active / recent work

- **Web PWA client:** implemented on `feat/web-pwa-client`. Remaining: Chromium LAN E2E, then STATUS → Shipped.
- Cross-desktop M2/M3 still open.

## Hardcoded constraints

- Streamer often needs root (`pkexec`) for uinput + evdi.
- `--web` requires `--tls` and a readable `--web-root` (staged under `~/.local/share/droppix/runtime/web` for AppImage/Flatpak).
- AppImage relocates streamer to `~/.local/share/droppix/runtime/`.
- Flatpak uses `flatpak-spawn --host`.

## Links

- [README.md](README.md)
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
- [docs/STATUS.md](docs/STATUS.md)
- [docs/WIRE.md](docs/WIRE.md)
- [web/README.md](web/README.md)
- [spec](docs/superpowers/specs/2026-07-18-web-pwa-client-design.md)

## Recent session notes

- **2026-07-18:** Implemented host-served Web PWA end-to-end on `feat/web-pwa-client` (WSS bridge, GUI URL/QR, TS client with video/audio/input/fullscreen/PWA, packaging hooks, WIRE/STATUS/ARCHITECTURE updates). Web unit tests pass; host C++ not built on this macOS agent (needs Linux/evdi).
- **2026-07-18:** Specced host-served Web PWA client design.
- **2026-07-18:** Added ARCHITECTURE.md; local-first remotes; Claude↔Cursor tooling sync.
