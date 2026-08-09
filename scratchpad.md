# droppix — scratchpad

## Project identity

Spacedesk-like system: turn an Android tablet (or Linux desktop client) into a **true extended monitor** for a Linux PC (KDE Plasma / KWin first; X11 backend also shipped), with touch, stylus, keyboard, orientation, mirror/extend, and audio out. Verified E2E on Nexus 10 (Android 5.1.1) + Plasma 6 over USB and WiFi. Optional **host-served web PWA** client on branch `feat/web-pwa-client`.

License: MIT

## Session note — 2026-07-21 (shipped to master)

Merged `feat/gui-log-console` → `master` (`646efa0`). Three things landed, 236/236 host tests:

- **In-GUI Debug log console (F12 dock)** — `host/gui/log_{entry,classify,buffer,forwarder,model,panel}.*`. Captures streamer stdout/stderr + GUI `qWarning`/`qCritical` (message-handler chained so terminal/journald still print), tagged by session+source, with search / level / source filters, autoscroll, copy, save.
- **Web PWA now actually serves in local builds** — root cause was `build_command` silently dropping `--web` because `web/dist` wasn't resolvable. Fix bakes `DROPPIX_SOURCE_WEB_DIR`, `stageWebAssets()` copies it to `~/.local/share/droppix/runtime/web` (root-readable for pkexec), and a loud warning fires (in the F12 console) when assets are missing.
- **Persistent Server toggle** — the old "Start streaming" button is now an on/off toggle: starts a `server:<port>` listener, re-arms after a device disconnects, guards fast-failed-starts (`shouldRearm`), and saves/restores state via a `configDir()/server_enabled` marker.

Gotcha: `droppix_gui` regenerates its cert every launch (`cert_.regenerate()`), so isolate `XDG_CONFIG_HOME` when smoke-testing to avoid clobbering the real config.

## Session note — 2026-07-24 (Communication Interfaces panel)

Shipped `feat/comm-interfaces` → `master`. spacedesk-inspired panel (from a spacedesk Driver Console screenshot):

- **Adapter list** — `host/gui/lan_ifaces.*` enumerates all up IPv4 adapters; per-adapter include checkboxes; the web URL/QR follows the first *included* adapter, so unchecking VMware/VPN adapters fixes the wrong-IP footgun. `included_ifaces()` is a pure, unit-tested filter (4 tests).
- **LAN / USB toggles** — LAN off stops mDNS browse + advertise; USB off stops the tether + AOA scanners (kills the 2s USB poll). OFF gates discovery + new auto-connects but keeps live sessions. Persisted via `configDir()` markers (`lan_disabled` / `usb_disabled` / `advertise_excluded_adapters`), restored on launch.

Honest scope: fixes the address droppix *shows* + stops scanners; does NOT change what avahi advertises (host A-records / client-side resolution) and sockets stay `INADDR_ANY`. 240/240 host tests.

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

- **2026-08-09:** Added `~/.local/bin/droppix` launcher (this dev machine, not part of the repo): checks the NAS checkout's HEAD against a `.built_commit` marker before launching `droppix_gui`, rebuilds+reinstalls automatically if source has moved on (falls back to the last-good binary on a build failure), instant launch otherwise. Prompted by the host binary silently drifting ~11 days behind the NAS source with no signal. Also: `StatusPage`'s Server control (which had *already* been redesigned into a single `serverSwitch()` checkable QPushButton by other work since the two-Start/Stop-button change below) is now a real sliding toggle switch — new `ToggleSwitch` (`host/gui/toggle_switch.*`, QAbstractButton subclass, painted track+knob, 150ms slide animation, QSS-settable colors via `qproperty-*`) replacing the old color-changing pill button. Visually verified via a temporary `QWidget::grab()` PNG dump (removed before commit). Commit `43d02fb`.
- **2026-07-26:** Fixed video squash/stretch over WiFi/Extend: `GlDisplayView`'s GL renderer drew a fixed -1..1 fullscreen quad with **no aspect-ratio-preserving fit at all** — any mismatch between the streamed resolution's exact pixel dims and the view's on-screen size (e.g. evdi/CVT not producing precisely the tablet's requested "native" aspect, common on modern non-16:9 phones) got non-uniformly stretched. New pure `AspectFit.scale()` (`android/.../ui/AspectFit.kt`, 8 unit tests, no Android imports) computes letterbox/pillarbox clip-space extents; `GlDisplayView` tracks measured view size + video size (from `StreamActivity.onConfig`'s `config.width/height`) and rewrites the quad each frame. Also two Active-monitors/Server-toggle UX fixes this session: separate Start/Stop buttons (was one checkable toggle), and the monitor list/status header now only count actually-approved devices — no more permanent "Server — waiting for a device…" placeholder row or "1 monitor · waiting" before anyone connects (`addMonitorRow`, `refreshPairingUi`-adjacent `updateStatus` rewrite, commits `fb3248d`/`72ba4bf`/`cfac61f`).
- **2026-07-26:** QR-code scan-to-pair + X11 evdi deadlock fix + orientation calibration fix, all ported into this repo (the canonical NAS checkout, `origin=Spinjitsudoom/droppix`) after being authored/tested in a disconnected local copy on another machine — see commits `2bfa940`, `872e795`, `c9e7500`, `06be33d`. **X11 evdi fix:** on Cinnamon/Xorg (confirmed via user log), evdi is a separate GPU provider; X never assigns it a mode until `xrandr --setprovideroutputsource` links it, but `EvdiFrameSource::start()` blocked in `wait_for_mode(5000)` *before* that ran (the enable step fired only after `start()` returned → permanent timeout loop, "source start failed" forever). Fixed via an `on_connected` callback run on a background thread concurrently with the mode-wait (new `DesktopBackend::link_providers()`, X11-only). Host-tested end-to-end on the reporting user's machine; still needs the same verification on another X11 box. **QR pairing:** `make_qr_image` had no quiet zone (scanners couldn't detect it against the dark UI); added a 4-module white border. Proactive scan-to-pair QR added to the "Active monitors" panel (`refreshPairingUi`), and the reactive "device connecting" popup now also shows a QR — both use the host's own LAN IP (via `included_ifaces`), not the connecting client's. **Orientation:** `OrientationMapper.QUARTER_TO_CODE` swapped 1↔3 (landscape/portrait were inverted on the reporting user's tablet). **Also:** restored the "Connect via USB (adb)" button (removed 2026-07-05 for USB-tether autoconnect) per explicit user request. zxing-android-embedded pinned to 3.6.0 (not 4.x, which needs minSdk 24) for Nexus 10 / API 22 support — see lesson `zxing-embedded-minsdk`. Android side unbuilt/unverified from this environment (no Android SDK here); host side built + 244/244 tests pass in `~/droppix-nas-build` (off-mount, matching the existing CIFS-mount build convention).
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
