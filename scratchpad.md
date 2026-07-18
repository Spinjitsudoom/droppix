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
| `tools/web-mock-host/` | Local HTTPS/WSS mock host for web PWA debug (no evdi) |
| `macos/` | Archived Mac host spike; see CGVirtualDisplay research |
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
- [web PWA design](docs/superpowers/specs/2026-07-18-web-pwa-client-design.md)
- [CGVirtualDisplay OSS research](docs/superpowers/specs/2026-07-18-cgvirtualdisplay-oss-research.md)

## Recent session notes

- **2026-07-18:** Lipsync via media PTS (no delay queues): video stamped at RGB read (`frameIndex/fps`); audio held and released only up to `lastVideoPts+40ms` (PTS pacing). Client paints video against audio wire clock. GEEKS shows VPTS/APTS/SKEW/DROP. Steady skew ~40ms. Encoder: ultrafast + low_delay + keyint_min=1.
- **2026-07-18:** Server-burned overlay: mock now decodes MP4 → composites click marks + event-log panel in Node (`mock-desktop.overlayFrame`, dimension-aware) → re-encodes to H.264 (`overlay-stream.mjs`, `createRgbEncoder`), so the overlay is IN the stream (true E2E), not client DOM. Client SRV-mark polling disabled via `burnIn` config; starts muted. Lipsync fixed: `-preset ultrafast` + drop-frame (not pause) backpressure + buffer pool hold source 24fps; client audio prebuffer 120→60ms. Lesson `mock-overlay-lipsync`. E2E green.
- **2026-07-18:** Autoplay-policy fix: real Chrome kept AudioContext suspended after mock auto-connect (no gesture). `await ctx.resume()` hangs forever under autoplay, which froze `connect()` and stuck the `connecting` flag → dead Connect button + no video. Fix: never await resume() (fire-and-forget + gesture hook), `connect()` in try/finally. Also silent audio the E2E missed because Playwright Chromium never enforces the policy (gotcha `playwright-autoplay`). Client resumes on first pointer/key gesture + "tap for audio" status hint.
- **2026-07-18:** Playback overhaul: sample is now a real movie w/ dialogue (Tears of Steel 110s segment, CC-BY; `fetch-sample.sh` downloads it). Fixed delta-frame drop corruption (drop-until-keyframe resync in `decoder.ts`), audio clicks (AudioWorklet-first w/ 120ms prebuffer; buffer fallback aggregates 200ms; chirp removed), and **ghost stream after disconnect** (stale-socket guards in `transport.ts`, single-flight connect in `main.ts`, single `activeSession` preemption in mock server + `/debug/session`). Lessons: `ghost-ws-stream`, `delta-drop-corruption`. E2E passes incl. stays-black-after-disconnect regression.
- **2026-07-18:** Mock host streams a looped MP4 (`assets/sample.mp4` or `DROPPIX_MOCK_MP4`) with synced A/V via one ffmpeg + fifos for lipsync checks. Idle stage black (no local mock wallpaper); SRV marks only while connected.
- **2026-07-18:** Blank-screen fixes: slices=1, SW network-first / unregister on :8443, auto-Connect on mock.
- **2026-07-18:** Mock E2E desktop input + `/debug/server-marks`; AudioBuffer path for self-signed TLS.
- **2026-07-18:** Mock UX: always-on 440 Hz PCM, AudioBuffer fallback (self-signed breaks AudioWorklet), mock waiting preview + log.
- **2026-07-18:** Playwright E2E for web PWA vs mock host: `tools/web-mock-host` `npm run test:e2e` (1 passed: Connect → canvas pixels → Touch/MouseButton/Scroll/Key via `/debug/inputs` → fit/mute → Disconnect). Cursor Playwright MCP blocked by self-signed cert (use the npm script).
- **2026-07-18:** Added `tools/web-mock-host` (HTTPS + WSS mock: testsrc H.264, PCM tone, input console logs) + skill `web-mock-host` for Mac-side web PWA debugging without Linux/evdi. PIN default `123456`, port `8443`.
- **2026-07-18:** Parked CGVirtualDisplay OSS research: [`docs/superpowers/specs/2026-07-18-cgvirtualdisplay-oss-research.md`](docs/superpowers/specs/2026-07-18-cgvirtualdisplay-oss-research.md). Decision: own thin `macos/` wrapper; crib DeskPad/VDK/daylight-mirror; reject BetterDisplay/DisplayLink deps.
- **2026-07-18:** Implemented host-served Web PWA end-to-end on `feat/web-pwa-client` (WSS bridge, GUI URL/QR, TS client with video/audio/input/fullscreen/PWA, packaging hooks, WIRE/STATUS/ARCHITECTURE updates). Web unit tests pass; host C++ not built on this macOS agent (needs Linux/evdi).
- **2026-07-18:** Specced host-served Web PWA client design.
- **2026-07-18:** Added ARCHITECTURE.md; local-first remotes; Claude↔Cursor tooling sync.
