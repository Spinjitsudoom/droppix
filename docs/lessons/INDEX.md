# Lessons index

## How to use

1. Grep this file for a failure tag (e.g. `silent-failure`, `oom`) or subsystem tag (e.g. `encoder`, `protocol`).
2. Open **only** the matching detail file(s). Do not load every lesson.

## Tag taxonomy

- **Subsystem:** `host`, `android`, `client`, `protocol`, `encoder`, `evdi`, `input`, `transport`, `gui`, `packaging`, `desktop-backend`, `audio`, `tls`
- **Failure class:** `silent-failure`, `data-loss`, `performance`, `oom`, `truncation`, `wrong-answer`, `regression`, `flake`, `gotcha`
- **Severity:** `critical`, `high`, `medium`, `low`
- **Doc type:** `lesson`, `constraint`, `gotcha`

## Lessons

| ID | Title | Tags | Severity | Date | File |
|---|---|---|---|---|---|
| L-2026-08-17-orientation-dims-one-way-swap | Landscape phone got a portrait strip + the session restarted every ~20s forever: the HELLO-dims-to-orientation swap was one-directional (assumed "the app always sends landscape dims", false for a portrait-natural phone whose `configChanges` stops the Activity recomputing them) | host, android, evdi, wrong-answer, regression | high | 2026-08-17 | [orientation-dims-one-way-swap.md](orientation-dims-one-way-swap.md) |
| G-2026-08-16-popen-inherits-sockets | `popen()` (parec for audio, xrandr for layout) inherits every non-CLOEXEC fd — parec ended up owning the listening socket, so a killed streamer left the port bound by an invisible process and SO_REUSEADDR could not help; use `SOCK_CLOEXEC`/`accept4` | host, transport, audio, silent-failure, gotcha | medium | 2026-08-16 | [popen-inherits-sockets.md](popen-inherits-sockets.md) |
| L-2026-08-16-usb-button-without-a-tunnel | The client's USB button dialled `127.0.0.1:27000` for 6 weeks with nothing creating the `adb reverse` tunnel — the host half was deleted with the adb path, the client half restored after; the orphaned `auto_adb_reverse` setting (written, loaded, never read) was the tell | host, gui, transport, android, silent-failure, regression | high | 2026-08-16 | [usb-button-without-a-tunnel.md](usb-button-without-a-tunnel.md) |
| G-2026-08-16-android-debug-vs-release-signing | `adb install` fails with an **empty** error: the phone runs a release-signed build, so `assembleDebug` can never update it — ask `pm` for the real reason instead of retrying, and never uninstall to "fix" it | android, packaging, gotcha, silent-failure | medium | 2026-08-16 | [android-debug-vs-release-signing.md](android-debug-vs-release-signing.md) |
| G-2026-08-15-static-screen-low-fps | "Low fps" on a static screen is correct (evdi capture is damage-driven); the constant ~1.5 Mbps baseline is uncompressed PCM audio, not video — always measure with motion | host, evdi, audio, performance, gotcha | medium | 2026-08-15 | [static-screen-looks-like-low-fps.md](static-screen-looks-like-low-fps.md) |
| L-2026-08-15-slow-link-bufferbloat | Fixed-bitrate encoding on a slow client link degrades invisibly: the kernel buffers the excess, so sends stay fast while latency grows and fps collapses — pace capture on SIOCOUTQ backlog | host, transport, performance, silent-failure | high | 2026-08-15 | [slow-link-bufferbloat.md](slow-link-bufferbloat.md) |
| L-2026-08-15-web-client-sigpipe-death | A web client disconnecting killed the whole streamer — unhandled SIGPIPE on SSL_write (exit 141, no log line) | host, transport, tls, silent-failure | critical | 2026-08-15 | [web-client-sigpipe-death.md](web-client-sigpipe-death.md) |
| G-2026-08-10-web-local-ca-trust | Browser-trusted HTTPS for a LAN-only host: persistent local CA signs a rotating leaf, served at /ca.crt | host, tls, gotcha | medium | 2026-08-10 | [web-local-ca-trust.md](web-local-ca-trust.md) |
| G-2026-08-06-web-mse-render | Low-end web clients choke on WebCodecs→Canvas 2D (per-frame copy, filter path); an MSE `<video>` path (client-side fMP4 mux) uses native hardware decode + compositor | client, performance, gotcha | medium | 2026-08-06 | [web-mse-render.md](web-mse-render.md) |
| G-2026-08-06-build-time-version | Embed `git describe` at build time behind a one-TU accessor (write-if-changed) so the version tracks the checkout without recompiling the GUI | gui, packaging, gotcha | low | 2026-08-06 | [build-time-version.md](build-time-version.md) |
| G-2026-08-05-web-h264-codec-level | Web client black-screen: hardcoded WebCodecs `avc1.42E01F` mismatches host's higher H.264 profile/level — derive from SPS | client, encoder, wrong-answer, silent-failure | high | 2026-08-05 | [web-h264-codec-level.md](web-h264-codec-level.md) |
| G-2026-08-03-qss-property-repolish | QSS `[prop="true"]` selectors don't re-apply after `setProperty()` alone — need `style()->unpolish/polish()` | gui, gotcha | medium | 2026-08-03 | [qss-property-repolish.md](qss-property-repolish.md) |
| G-2026-08-02-web-dist-committed-sw-cache | web/dist is committed (rebuild off-mount) and sw.js caches the shell (bump CACHE) | client, packaging, gotcha, silent-failure | medium | 2026-08-02 | [web-dist-committed-sw-cache.md](web-dist-committed-sw-cache.md) |
| L-2026-07-26-x11-evdi-prime-deadlock | evdi never gets a mode on X11 (Cinnamon/XFCE/etc) — ordering deadlock; **updated 2026-08-10**: ordering fix alone wasn't enough, also fixed a 5s/10s timeout race + an orphaned-WebSocket-on-failure bug | host, evdi, desktop-backend, transport, silent-failure | high | 2026-07-26 | [x11-evdi-prime-deadlock.md](x11-evdi-prime-deadlock.md) |
| G-2026-07-19-zxing-embedded-minsdk | zxing-android-embedded 4.x needs minSdk 24 (crashes on Nexus 10 / API 22) | android, packaging, gotcha | high | 2026-07-19 | [zxing-embedded-minsdk.md](zxing-embedded-minsdk.md) |
| L-2026-07-18-ghost-ws-stream | Video keeps playing after disconnect (orphaned WebSocket) | client, transport, silent-failure | high | 2026-07-18 | [ghost-ws-stream.md](ghost-ws-stream.md) |
| L-2026-07-18-delta-drop-corruption | Shaky/corrupted video from dropping delta frames | client, encoder, wrong-answer, performance | high | 2026-07-18 | [delta-drop-corruption.md](delta-drop-corruption.md) |
| G-2026-07-18-playwright-autoplay | Playwright Chromium never suspends AudioContext; `await resume()` hangs connect under real autoplay | client, audio, flake | high | 2026-07-18 | [playwright-autoplay.md](playwright-autoplay.md) |
| L-2026-07-18-mock-overlay-lipsync | A/V drift when re-encoding movie with burned-in overlay (preset/backpressure) | encoder, audio, performance | medium | 2026-07-18 | [mock-overlay-lipsync.md](mock-overlay-lipsync.md) |

## Related external docs

- [../ARCHITECTURE.md](../ARCHITECTURE.md) — system architecture
- [../STATUS.md](../STATUS.md) — feature / design status
- [../WIRE.md](../WIRE.md) — current protocol
- [../../scratchpad.md](../../scratchpad.md) — session memory
- [../README.md](../README.md) — docs hub

## Adding a new lesson

Copy [`_TEMPLATE.md`](_TEMPLATE.md). Use IDs:

- `L-YYYY-MM-DD-shortslug` — fixed mistakes
- `C-YYYY-MM-DD-shortslug` — hard constraints
- `G-YYYY-MM-DD-shortslug` — gotchas

Add one row to the table above. Keep the detail file short; link to code/commits.
