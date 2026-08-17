# droppix — feature & docs status

**Last verified:** 2026-08-16 on `master` (335/335 host+gui tests, 0 skipped — run where `openssl` is present; the `droppix-dev` container has no openssl, so the two `CertManager` tests and `WebFrontend.LoadCertDerAndPairingCode` silently SKIP there). This commit lands work that had been sitting uncommitted: the **local CA** (`CertManager` signs the per-launch leaf with a persistent, never-rotated CA, served unauthenticated at `GET /ca.crt`; install it once and every future connection is trusted with no browser warning — lesson `web-local-ca-trust`) and the X11 evdi mode-wait fix (5s wait raced its own `runuser`/PAM provider-link thread's 10s budget and lost; now 12s — lesson `x11-evdi-prime-deadlock`). Also on 2026-08-16: the client's USB button had no `adb reverse` tunnel behind it for six weeks (lesson `usb-button-without-a-tunnel`). 2026-08-06 baseline: build-time version string (`git describe`) in the GUI title bar + About; host-verified web PIN pairing — page loads/connects live, host **holds the stream** until the browser submits the code shown on the PC, verified host-side (`web_pin.h`, constant-time), 5-attempt limit then drop; code never sent to the browser. Earlier on master: host GUI redesign (sectioned `QStackedWidget`, Status hero + live metrics, dual theme); web PWA client redesign (in-session control bar, settings drawer, dual theme); live server refresh + prominent Copy-web-URL.

Living source of truth for "is this designed / planned / shipped?". Design specs under `superpowers/specs/` keep their historical detail; this file overrides stale **Status** lines until those headers catch up.

## Product maturity

| Area | State |
|---|---|
| Core extend path (evdi → H.264 → Android MediaCodec) | **Shipped** |
| Host GUI (Qt6 multi-session control panel) | **Shipped** — redesigned (2026-08-03) into spacedesk-style sections (Status/Connections/Interfaces/Settings/About) behind a `QStackedWidget`, with a Status hero (server ON/OFF switch + live monitors/clients/interfaces metrics + scan-to-pair), dark+light themes with a persisted in-app toggle, and previously-hidden resolution/fps/audio/orientation settings surfaced in Settings |
| Android client (minSdk 21) | **Shipped** |
| Linux desktop receive client (`client/`) | **Shipped** |
| Packaging (AppImage + Flatpak host/client, APK script) | **Shipped** |
| macOS host backend | **Archived** (`macos/`; not in build). CGVirtualDisplay OSS research: `2026-07-18-cgvirtualdisplay-oss-research.md` |
| Cross-desktop beyond KWin | **Partial** — M1 seam + X11 backend shipped; Sway/GNOME Wayland still open. X11 reverse-PRIME evdi (separate-GPU provider, e.g. Cinnamon/Xorg) fixed 2026-07-26: provider link + auto-enable now run concurrently with the mode-wait, not after |
| Web PWA client (host-served) | **Shipped** — browser-verified over HTTPS/WSS; local builds auto-stage `web/dist` to the runtime dir; redesigned (2026-08-04) into a spacedesk-style shell: connect card that auto-connects on load, a post-connect **host-verified 6-digit PIN overlay** (page connects live, stream held until the code shown on the PC is entered — see the pairing row below), an in-session auto-hiding player-style control bar, a settings drawer surfacing every client setting (renderer [WebCodecs-canvas / native-video-MSE], resolution [Auto/1080p/720p/540p/480p, default 720p], quality, fps, audio, fit, flip, brightness, contrast, wall, device name), and a dark/light theme with a persisted toggle |
| In-GUI Debug log console (F12) | **Shipped** — dockable panel capturing streamer + GUI logs |

## Feature matrix (code on master)

| Feature | Status | Primary code |
|---|---|---|
| Extended virtual display (evdi) | Shipped | `host/src/evdi_frame_source.*`, `virtual_display.*` |
| H.264 encode Autocascade (NVENC → VAAPI → x264) | Shipped | `host/src/{encoder_factory,nvenc_encoder,vaapi_encoder,software_encoder}.*` |
| Wire protocol v6 / client-declared monitor grid | Shipped end-to-end (host + both clients); on-device 2-tablet grid verification pending. `stream_daemon` reads `wall_col`/`wall_row` off HELLO and places the evdi output via `grid_position`/`DesktopBackend::place_output` (extend only — skipped in mirror mode, where the output is overlaid on the primary); `(0,0)` reproduces today's right-of-primary placement, so no behavior change until a client sends non-zero cells. All three clients send it (default `(0,0)`): Android has a "Wall position" settings row (`wallCol`/`wallRow`, persisted); the Linux desktop client (`client/`) has a "Wall column"/"Wall row" settings-dialog row (`ClientSettings::wall_col/wall_row`, persisted); the web PWA client (`web/`) has "Wall" col/row number inputs in the toolbar (`ClientSettings.wallCol/wallRow` in localStorage) and now speaks HELLO v6 (bumped from v5). | `host/src/protocol.*`, `host/src/{transport_server,stream_daemon,desktop_backend}.*`, `android/.../protocol/Protocol.kt`, `android/.../settings/AppSettings.kt`, `android/.../net/TransportClient.kt`, `client/src/{client_settings,transport_client}.*`, `client/gui/{settings_dialog,main_window}.cpp`, `web/src/{protocol,transport,settings}.ts`, `web/src/main.ts`, `web/public/index.html` |
| Touch + multi-touch + 2-finger right-click | Shipped | `input_injector.*`, `mt_slots.*`, `tap_gesture.*` |
| Stylus (pressure + eraser) | Shipped | `MsgType::Pen`, `map_pen`, Android pen path |
| Keyboard + on-screen keyboard | Shipped | `MsgType::Key`, Android `KeyMap` / soft keyboard |
| Mouse scroll / buttons | Shipped | `MsgType::Scroll`, `MouseButton` |
| Auto-orientation | Shipped | `orientation.h`, `MsgType::Orientation` |
| Mirror / extend layout toggle | Shipped | `LayoutMode`, `DesktopBackend::apply_layout` |
| Audio to tablet (PipeWire) | Shipped | `audio_streamer.*`, `audio_sink.*`. Latency is bounded in **time** at every stage (`host/src/audio_latency.h`, `web/src/audio-policy.ts`, Android `AudioPlayer`): `parec --latency-msec=20`, a 200 ms host queue, ~256 ms client queue, 250 ms web schedule lead — each recovering to a low-water mark on overflow, since a backlog is never caught up at real-time playback. See lesson `audio-latency-grows-unbounded` |
| WiFi discovery + TLS PIN pairing | Shipped | `mdns_*`, `cert_manager.*`, Android `TlsTrust` |
| Local CA for browser-trusted web HTTPS (no cert warning after one-time install) | Shipped | `host/gui/cert_manager.*` (CA + SAN-covered leaf), `host/src/web_frontend.cpp` (`GET /ca.crt`), `web/public/index.html` install link |
| QR-code pairing (scan to skip PIN typing) | Shipped (host); Android build/on-device retest pending | `host/src/qr_generator.*`, `main_window::refreshPairingUi`/`showPairingPopup`; Android `net/QrUri.kt`, ZXing scan in `ui/ConnectActivity.kt` |
| USB `adb reverse` | Shipped | `host/gui/adb_transport.*` — discovers attached tablets, keeps each one's reverse tunnel alive, and **connects from the PC**: picking the row tunnels that session's port and `am start`s the client with `usb_autoconnect`/`usb_port`, so the tablet is never touched. Client also keeps its own "Connect via USB" button. **Host side was missing 2026-07-05 → 2026-08-16**: `AdbManager` was deleted with the adb USB path (`a349ee5`), the client button was restored 2026-07-26 still dialling `127.0.0.1:27000`, and nothing created the tunnel — see lesson `usb-button-without-a-tunnel` |
| USB tethering transport | Shipped | `tether_discovery.*`, `TetherProbe.kt` |
| AOA USB accessory transport | Shipped | `aoa_{channel,connect,scan}.*`, Android `UsbAccessory`. Unplugging removes the monitor: the GUI watches the scan for a live session's serial going missing and stops it (`host/gui/aoa_presence.h`, debounced — the accessory-mode switch re-enumerates the device and a marginal cable drops it repeatedly). Nothing else notices an unplug: the streamer's reconnect loop retries the handshake forever |
| Client-initiated disconnect | Shipped | `MsgType::Bye` (6) from the client's floating menu → `TransportServer::said_bye()` → streamer exits its reconnect loop → GUI removes the monitor. Distinct from a dropped link, which the host still treats as "wait for them to come back" |
| Multi-monitor (N tablets) | Shipped | `session_manager.*`, `port_alloc.*` |
| Auto-connect known monitors | Shipped | `auto_connect.*` |
| Client-owned display settings (HELLO v4/v5) | Shipped | Android `AppSettings`, `client_settings.*` |
| Quality / rotation lock / stats overlay | Shipped | Android settings + `MsgType::Overlay` |
| Brightness / contrast (client-side) | Shipped | `GlDisplayView`, client `adjust_luma` |
| Render-stage horizontal flip | Shipped | Android + desktop client flip |
| DesktopBackend (KWin / X11 / Generic) | Shipped (M1+) | `desktop_backend.*` |
| Sway / GNOME Wayland backends | Roadmap | see cross-desktop spec |
| Web PWA client (host-served HTTPS + WSS) | Shipped | `web/`, `host/src/web_frontend.*`, `host/src/ws_channel.*`, GUI web-root staging |
| In-GUI Debug log console (F12 dock) | Shipped | `host/gui/log_{buffer,classify,forwarder,model,panel}.*` |
| Persistent Server toggle (re-arm + restore) | Shipped | `host/gui/server_control.*`, `host/gui/main_window.*` |
| Communication Interfaces panel (adapter list + LAN/USB toggles) | Shipped | `host/gui/lan_ifaces.*`, `host/gui/main_window.*` |
| Host GUI redesign (sectioned `QStackedWidget`, Status hero, dual theme) | Shipped | `host/gui/pages/{status,connections,interfaces,settings,about}_page.*`, `host/gui/style.h`, `host/gui/theme{,_pref}.*`, `host/gui/main_window.*` |
| Live server refresh + prominent Copy web URL | Shipped | With the Server ON, changing any streamer setting (`SettingsPage::settingsChanged`) or Interfaces toggle restarts the listener (debounced 600 ms, race-safe one-shot restart) so it applies immediately; a "Copy web URL" button on Status + Interfaces surfaces `currentWebUrl()` whenever server + web client are on. `host/gui/pages/settings_page.*`, `host/gui/pages/status_page.*`, `host/gui/main_window.*` |
| Web PWA client redesign (auto-connect connect card, in-session control bar, settings drawer, dual theme) | Shipped | `web/src/{theme,connect-view,session-controls,settings-drawer}.ts`, `web/public/{index.html,styles.css}` |
| Build version in GUI (title bar + About) | Shipped | Resolved at build time from `git describe --tags --always --dirty` (e.g. `v0.1.0-121-ge6bd3ca`), so every build is identifiable even when the UI is unchanged; falls back to static `0.1.0` for git-less tarball builds. Regenerated write-if-changed; only `version.cpp` includes the generated header, so a version bump relinks one TU, not the GUI. `host/cmake/GenerateVersion.cmake`, `host/src/version.{h,cpp}`, `host/gui/main_window.cpp` (title + About dialog), `host/gui/pages/about_page.cpp`, `host/tests/test_version.cpp` |
| Web MSE render path (native `<video>` decode, alt to WebCodecs/canvas) | Shipped | For low-end clients (e.g. Redmi 9) the WebCodecs→Canvas 2D path is CPU/GPU-bound at a few fps; MSE muxes H.264 into fragmented MP4 client-side (`fmp4.ts`, host protocol unchanged) and feeds a `<video>` so the phone's native hardware decoder + compositor render it. Selectable via Settings → Stream → Renderer (default WebCodecs); the canvas stays on top as a transparent input overlay. Audio stays on the separate PCM path. `web/src/{fmp4,mse-decoder,video-renderer}.ts`, `web/src/main.ts`, `web/public/{index.html,styles.css}`, tests `web/tests/fmp4.test.ts` |
| Selectable web stream resolution (Auto/1080p/720p/540p/480p, default 720p) | Shipped | Caps what the host renders instead of always requesting canvas×devicePixelRatio; `web/src/settings.ts` `resolveResolution()`, `web/tests/settings.test.ts` |
| Host-verified web PIN pairing (post-connect, stream held) | Shipped | Page connects live; host holds video until the browser submits the code shown on the PC. Verified host-side (constant-time), 5-attempt limit then drop; code never sent to the browser. `Pair`/`PairResult` are WSS-only wire types (20/21). `host/src/web_pin.h`, `host/src/web_frontend.cpp` (`verify_web_pin`), `host/tests/test_web_pin.cpp`, `web/src/{protocol,transport}.ts`, `web/src/main.ts`, `web/public/{index.html,styles.css}`; mock pair flow via `PAIR=1` in `tools/web-mock-host` |
| Zero-copy GPU capture | Out of scope (for now) | evdi still delivers CPU BGRA frames |

## Spec index (status as of 2026-07-18)

| Spec | Verdict |
|---|---|
| `2026-06-23-android-extended-display-design.md` | Shipped |
| `2026-06-23-droppix-wire-protocol.md` | Shipped (superseded in detail by `protocol.h` v5; see [WIRE.md](WIRE.md)) |
| `2026-06-23-phase0-spike-findings.md` | Findings (historical) |
| `2026-06-23-phase1b-device-findings.md` | Findings (historical) |
| `2026-06-24-configurable-resolution-refresh-design.md` | Shipped |
| `2026-06-24-host-gui-design.md` | Shipped |
| `2026-06-24-latency-baseline-findings.md` | Findings (historical) |
| `2026-06-24-touch-input-design.md` | Shipped |
| `2026-06-27-auto-orientation-design.md` | Shipped |
| `2026-06-27-gui-restyle-design.md` | Shipped |
| `2026-06-27-wifi-discovery-client-gui-design.md` | Shipped |
| `2026-06-28-tls-pin-pairing-design.md` | Shipped |
| `2026-06-29-audio-output-design.md` | Shipped |
| `2026-06-30-client-discovery-usb-design.md` | Shipped |
| `2026-07-02-fat-appimage-design.md` | Shipped |
| `2026-07-02-flatpak-design.md` | Shipped |
| `2026-07-02-multi-touch-design.md` | Shipped |
| `2026-07-02-pairing-code-ux-design.md` | Shipped |
| `2026-07-03-multi-monitor-design.md` | Shipped |
| `2026-07-03-two-finger-rightclick-design.md` | Shipped |
| `2026-07-04-auto-connect-known-monitors-design.md` | Shipped |
| `2026-07-05-aoa-usb-transport-design.md` | Shipped |
| `2026-07-05-cross-desktop-portability-design.md` | Partial — M1 + X11 done; M2/M3 open |
| `2026-07-05-usb-tethering-transport-design.md` | Shipped |
| `2026-07-06-aoa-m3-gui-usb-detection-design.md` | Shipped |
| `2026-07-06-desktop-backend-m1-design.md` | Shipped |
| `2026-07-10-client-owned-display-settings-design.md` | Shipped |
| `2026-07-11-quality-rotationlock-overlay-design.md` | Shipped |
| `2026-07-12-brightness-contrast-design.md` | Shipped |
| `2026-07-12-mouse-input-design.md` | Shipped |
| `2026-07-12-render-stage-flip-design.md` | Shipped |
| `2026-07-13-keyboard-input-design.md` | Shipped |
| `2026-07-13-mirror-mode-design.md` | Shipped |
| `2026-07-13-onscreen-keyboard-design.md` | Shipped |
| `2026-07-13-stylus-design.md` | Shipped |
| `2026-07-16-hw-encode-design.md` | Shipped |
| `2026-07-18-web-pwa-client-design.md` | Shipped — host-served, browser-verified |
| `2026-07-18-gui-log-console-design.md` | Shipped |
| `2026-07-18-cgvirtualdisplay-oss-research.md` | Findings — own thin `macos/` wrapper; DeskPad/VDK/daylight-mirror as cribs; no BetterDisplay/DisplayLink dep |
| `2026-07-20-server-toggle-design.md` | Shipped |
| `2026-07-21-comm-interfaces-design.md` | Shipped |
| `2026-08-03-host-gui-redesign-design.md` | Shipped |

## How to keep this current

When a feature lands on `master`:

1. Update the matching row here to **Shipped** (or **Partial** with what remains).
2. Set the design spec header to `**Status:** Shipped (YYYY-MM-DD).`
3. Leave the historical plan as-is (plans are build journals; do not rewrite mid-flight checklists after the fact unless fixing factual errors).
