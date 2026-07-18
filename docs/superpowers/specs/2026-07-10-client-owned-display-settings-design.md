# Client-owned display settings

**Date:** 2026-07-10
**Status:** Shipped on master (HELLO v4/v5).

## Summary

Move the four per-display settings — **resolution, FPS, audio, rotation** — off the
host GUI and onto the clients (the devices acting as monitors), Spacedesk-style. Each
client chooses its own settings and communicates them to the host in the connection
handshake. The host stops offering these four knobs.

Applies to **all clients**: the Linux `droppix_client` and the Android app.

## Background (current behavior)

- **Resolution** is *already* client-driven: the client sends `width/height` in HELLO and
  the host builds the evdi virtual monitor to match ([stream_daemon.cpp:47-51]). Both
  clients currently hardcode 1920×1080; the host `Settings` resolution is only a fallback
  used when the client reports 0.
- **Rotation** already has a live path: the client sends `ORIENTATION` messages (app→host);
  the host's "Orientation (default)" is just the startup default. Android auto-rotates from
  its accelerometer.
- **FPS** and **Audio** are host-only today (`cfg_.fps`, `cfg_.audio`); the client sends
  nothing, so these need new wire fields.
- Host audio is decided at streamer launch: `args_builder` passes `--audio` from
  `Settings.audio`; the GUI creates the `droppix-audio` PipeWire null-sink per session
  ([main_window.cpp:273]); audio is limited to one session at a time ([main_window.cpp:660]).

## Decisions

| Question | Decision |
| --- | --- |
| Which clients | All of them (Linux `droppix_client` + Android). Host controls fully removed. |
| Resolution model | Dropdown of presets, **defaulting to the device's native screen resolution**. |
| Rotation | Linux client: manual picker (0/90/180/270). **Android: unchanged** (keeps sensor auto-rotate, no picker). |
| Apply timing | **Immediately** — a mid-stream change drops and reconnects (~1s) so the new HELLO applies. |
| Transport mechanism | **Extend the HELLO handshake** (Approach A). No new message types. |

## Approach A: extend HELLO

The host already reads everything it needs to start a session from HELLO. Add the missing
fields there rather than introducing new messages or round-trips. Back-compat is a version
check: an un-updated (v3) client falls back to host defaults.

## Protocol (`host/src/protocol.{h,cpp}`)

- Bump `kProtocolVersion` 3 → 4.
- New HELLO body layout (all integers big-endian):
  `u32 version, u32 width, u32 height, u32 density, u32 fps, u8 audio_wanted,
   u8 orientation_code, u16-len name, u16-len id`.
  The three new fixed fields sit after `density`, before the strings.
- `decode_hello` version-gates the new fields: `version ≥ 4` reads them; a v3/v2 body yields
  the sentinels `fps=0`, `audio_wanted=0`, `orientation_code=0` ("unspecified"). `encode_hello`
  gains `fps`, `audio_wanted`, `orientation_code` params.
- `CONFIG` (host→client) and `ORIENTATION` (client→host, live rotation) are unchanged.

## Host

### Streamer (`host/src/stream_daemon.cpp`, `transport_server.{h,cpp}`)

- `read_hello` grows three out-params: `fps`, `audio_wanted`, `orientation`.
- A pure, unit-tested helper picks the effective session parameters:
  `select_session_params(cver, hfps, haudio, hori, defaults) → {fps, audio, orientation}`
  — prefer the HELLO values when `cver ≥ 4` and non-sentinel, else the `cfg_` fallback.
  (Mirrors the codebase's existing pure selectors `select_backend_kind` / `devicesToConnect`.)
- Wire the results in: `sess_fps` → `enc_.open` + `send_config`; `sess_orientation` seeds the
  initial `ocode` (live changes still via the `ORIENTATION` handler); `sess_audio` gates the
  audio capture/stream path (replacing `cfg_.audio`).

### Audio provisioning

- The streamer now enables audio from HELLO, not from a launch arg, so the host must be ready
  regardless: the GUI always `ensure()`s the `droppix-audio` sink for any session (cheap
  null-sink), and `args_builder` stops gating `--audio` on `Settings.audio`.
- The existing **one-audio-session-at-a-time** limit is preserved: the first client to request
  audio holds it; while it is held, a later requester is refused audio (its video still works),
  and audio frees when that session ends.

### GUI (`host/gui/settings_dialog.cpp`, `settings.h`)

- Remove the four rows from the dialog — **Resolution, FPS, Audio, Orientation** — and their
  `load`/`store` lines.
- Keep the corresponding `Settings` fields as internal fallback defaults for pre-v4 clients;
  `profile_store` keeps persisting them harmlessly.
- Untouched host controls: Source (test/evdi), Refresh (Hz), Bitrate/Port (hidden), Touch,
  Performance Overlay, Auto-connect, and the app-level prefs.

## Linux client (`droppix_client`)

- **Settings store** — new `client_settings.{h,cpp}`, persisted via `QSettings` under
  `xdg-config/droppix_client`: `width/height` (default = native screen), `fps` (default 60),
  `audio` (default off), `rotation` (default 0°).
- **Settings UI** — a "Settings" toolbar action opening a dialog: Resolution dropdown (preset
  list, defaulting to the detected native resolution), FPS dropdown (30/60), Audio toggle,
  Rotation dropdown (0/90/180/270).
- **Native detection** — default resolution from `QGuiApplication::primaryScreen()->geometry()`.
- **Wire-up** — `main_window` stops hardcoding `1920,1080` and passes the settings into
  `runOverChannel`, which grows to take `fps`/`audio`/`orientation` and encodes HELLO v4.
  Rotation rides in HELLO — the host produces the rotated stream and the `QVideoWidget` just
  displays it, so no client-side video rotation is needed and this client sends no
  `ORIENTATION` message.
- **Auto-reconnect** — changing any setting while streaming calls `stopSession()` +
  `startSession(currentHost, port)` so the new HELLO applies.

## Android app

- **Settings UI** — a settings section on the main screen (SharedPreferences-backed):
  Resolution dropdown (default native device size), FPS dropdown (30/60), Audio toggle.
  **Rotation unchanged** — keeps the accelerometer auto-rotate and its live `ORIENTATION`
  messages.
- **Wire-up** — `StreamActivity` sends the chosen resolution (default native, instead of the
  hardcoded 1920×1080) + `fps` + `audio` + current `orientation_code` in HELLO v4.
- **Auto-reconnect** — a mid-stream settings change reconnects to apply; edited-before-connect
  is the common path.

## Testing

- **Protocol** — `encode/decode_hello` v4 round-trip tests + a back-compat test (a v3 body
  decodes to sentinels), extending the existing protocol tests.
- **Host** — unit tests for the pure `select_session_params` helper (HELLO-vs-fallback matrix).
- **Linux client** — a settings-store persistence round-trip test + a native-default test
  (GoogleTest, alongside the existing client tests).
- **Android** — a light unit test for the HELLO builder if practical; otherwise manual.
- **Manual E2E** — Linux client at 1280×720 / 30fps / audio-on / portrait → host builds a
  matching monitor; flip FPS → auto-reconnect resumes at the new rate.

## Out of scope

- No new settings beyond the four. Bitrate/port/refresh/touch/overlay/source stay host-side.
- No multi-consumer audio (the single-audio-session limit is retained, not expanded).

[stream_daemon.cpp:47-51]: ../../host/src/stream_daemon.cpp
[main_window.cpp:273]: ../../host/gui/main_window.cpp
[main_window.cpp:660]: ../../host/gui/main_window.cpp
