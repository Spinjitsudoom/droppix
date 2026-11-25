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
| L-2026-11-24-mdns-announce-outlives-gui | Every killed GUI (logout, `kill`, crash) left its `avahi-publish-service` running forever: Qt stops children only from destructors. `PR_SET_PDEATHSIG` via `endWithParent`, and SIGTERM/INT/HUP now run the tray-Quit shutdown | gui, transport, silent-failure | medium | 2026-11-24 | [mdns-announce-outlives-gui.md](mdns-announce-outlives-gui.md) |
| G-2026-11-24-interrupted-build-empty-objects | A distrobox restart mid-build left 0-byte `.o` files and binaries with fresh mtimes; ctest reported "100% passed" on a suite that had lost most of its tests. Compare the total with STATUS | packaging, silent-failure, gotcha | medium | 2026-11-24 | [interrupted-build-empty-objects.md](interrupted-build-empty-objects.md) |
| L-2026-11-24-vanished-tablet-held-session | A tablet that dropped off Wi-Fi held the session (and blocked its own reconnect) for minutes: no keep-alive, and backlog pacing meant no send ever failed. Keep-alives plus `TCP_USER_TIMEOUT` on every accepted socket | host, transport, silent-failure | high | 2026-11-24 | [vanished-tablet-held-session.md](vanished-tablet-held-session.md) |
| L-2026-11-25-libinput-rejects-pen | libinput (GNOME and KDE) ignored the droppix pen: no `BTN_STYLUS`, no X/Y resolution. The stylus never reached any compositor; `pen_device_caps()` now declares both | host, input, pen, silent-failure | high | 2026-11-25 | [libinput-rejects-pen.md](libinput-rejects-pen.md) |
| L-2026-11-25-gnome-reconnect-no-mode | On a quick reconnect GNOME still listed the droppix monitor as on and never moded it, and the stale entry broke identification. Revive via off/on temporary configs after 3 s; identify by EDID serial | host, evdi, gnome, silent-failure | high | 2026-11-25 | [gnome-reconnect-no-mode.md](gnome-reconnect-no-mode.md) |
| L-2026-11-24-evdi-mode-change-stale-buffer | A mid-session screen mode change kept the old-size evdi buffer registered but labelled frames with the new size, so the encoder could read past the buffer. Re-register, and restart the session when the frame no longer fits | host, evdi, encoder, wrong-answer | high | 2026-11-24 | [evdi-mode-change-stale-buffer.md](evdi-mode-change-stale-buffer.md) |
| L-2026-11-24-kscreen-enabled-substring | Two-finger right-click opened on another monitor: the kscreen parser treated a DISABLED output as enabled because its `HDR: enabled` line matched a substring, so the right-click pointer's range was 3840 on a 2688 desktop (x came out ×0.7) | host, desktop-backend, input, wrong-answer | high | 2026-11-24 | [kscreen-enabled-substring.md](kscreen-enabled-substring.md) |
| L-2026-11-24-ime-auto-shows-on-tablet | The soft keyboard opened by itself on every stream start (text-editor surface + unset soft-input state on a large screen), covering half the tablet so touches typed instead of reaching the PC; `SOFT_INPUT_STATE_ALWAYS_HIDDEN` | android, input, wrong-answer | high | 2026-11-24 | [ime-auto-shows-on-tablet.md](ime-auto-shows-on-tablet.md) |
| L-2026-11-24-hardcoded-wayland-0 | Touch silently disabled: root streamer assumed `WAYLAND_DISPLAY=wayland-0`, Plasma was on `wayland-1`, kscreen returned 0 bytes, so the droppix output was never identified and touch was skipped on purpose; discover the socket from the runtime dir | host, desktop-backend, input, silent-failure | high | 2026-11-24 | [hardcoded-wayland-0.md](hardcoded-wayland-0.md) |
| L-2026-11-24-aoa-chain-of-silent-stops | AOA "only worked with USB debugging": four independent stops: tablet had no USB function without adb (`18d1:4ee7` = debug only, enable MTP), app only adopted the accessory at plug-in, an AOA session took the Server's base port, evdi module never loaded | host, gui, android, transport, evdi, silent-failure | high | 2026-11-24 | [aoa-chain-of-silent-stops.md](aoa-chain-of-silent-stops.md) |
| L-2026-11-24-appimage-stale-streamer | The AppImage re-staged its root streamer only when the copy was older by mtime, but copies are stamped at extraction time, so it could run another build's streamer; compare bytes. The polkit script also authorized only the build-dir path, not the relocated one | gui, packaging, silent-failure, regression | high | 2026-11-24 | [appimage-stale-streamer.md](appimage-stale-streamer.md) |
| L-2026-11-24-config-dir-follows-exe-name | Each AppImage filename got its own empty `~/.config/<file>.AppImage` (no `setApplicationName`), so new builds silently lost all settings and fell back to test-pattern | gui, packaging, data-loss, silent-failure | high | 2026-11-24 | [config-dir-follows-exe-name.md](config-dir-follows-exe-name.md) |
| L-2026-08-17-audio-latency-grows-unbounded | Audio drifted seconds behind on both clients and never recovered: four buffers on the path, all bounded in bytes/items, none in milliseconds — and drop-oldest-one-on-overflow left queues pinned at the ceiling. Playback advances only in real time, so a backlog is never caught up; bound the duration and recover to a low-water mark | host, android, client, audio, performance, wrong-answer | high | 2026-08-17 | [audio-latency-grows-unbounded.md](audio-latency-grows-unbounded.md) |
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
