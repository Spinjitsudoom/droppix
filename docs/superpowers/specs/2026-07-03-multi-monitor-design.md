# Multi-monitor (multiple tablets, each a native-resolution extended monitor) — design

**Date:** 2026-07-03
**Status:** approved

## Goal

Let several tablets connect at once, each becoming its **own** extended monitor at its
**native resolution**, with its own touch. The host runs one streamer per tablet; the GUI
manages the set of sessions. Cap ~4 monitors.

## Why this shape

Each `droppix_stream` is already self-contained (own port, own evdi monitor, own touch
device). Multi-monitor is therefore "run N of them + manage them", not a streamer rewrite —
plus two streamer changes (native resolution, unique touch name) and a GUI session model.

## Components

### A. Streamer — create the evdi monitor AFTER HELLO (native resolution)

Today `StreamDaemon` gets a pre-built `FrameSource` sized from CLI flags, created before the
client connects. Change it to take a **source factory** and build the source post-HELLO:

- `StreamDaemon` constructor takes `std::function<std::unique_ptr<FrameSource>(int w, int h)>`
  instead of `FrameSource&`. It owns the source's lifetime.
- `run_until` order becomes: `accept_client` → `read_hello(cw, ch, …)` → approve gate →
  compute session dims = orientation-swap(`cw`, `ch`) using the current orientation code →
  `factory(sw, sh)` → `source->start()` → `enc_.open` → `send_config` → identify output /
  orientation / touch → stream loop.
- `stream_main` supplies the factory: `--test-pattern` → `TestPatternSource(w,h,fps)`; else
  `EvdiFrameSource(w,h,refresh)`. The `w,h` come from HELLO, not `--width/--height` (those
  become the fallback when a client reports 0). The orientation-restart path (portrait↔
  landscape crossing) still ends the session and rebuilds via the factory on reconnect.
- Existing single-session behaviour is preserved (one tablet → one native monitor).

### B. Streamer — unique touch device name

- `InputInjector::open(const std::string& name)` (was hardcoded `"droppix-touch"`).
- `stream_main` gains `--touch-name <name>` (default `droppix-touch`); `StreamConfig.touch_name`.
- `bind_touch_to_output(output_name, touch_name)` targets that exact name in KWin's ListTouch,
  so each session's touch device binds to its own output. The GUI passes
  `droppix-touch-<port>` per session.

### C. GUI — sessions instead of one stream

- New `SessionManager` (host/gui) owning `QList<Session>`, `Session = { StreamController*
  controller, int port, QString touchName, QString deviceLabel, QString transport, QString key }`.
- Pure, unit-tested port allocator: `int allocate_port(int base, const std::set<int>& used)` →
  lowest free `base + k`.
- `onConnectToSelectedDevice`: if the client already has a live session, no-op; else allocate a
  port, create a `Session` (new `StreamController`), `build_command` with that port + a unique
  `--touch-name` + shared settings (resolution now ignored for evdi — native), start it, then
  direct that tablet to that port (network → WAKE(port); USB → `adb reverse` + `am start` with
  the port — both already per-port). Add the session to the list + panel.
- Per-session signal routing: each `StreamController`'s `approvalRequested` / `runningChanged`
  / `connecting` / `statsReceived` / `logLine` is wired with the session as context (the
  approve dialog, pairing popup, and stop all act on the right session). Move the current
  single-`controller_` wiring into a per-session `wireSession()`.
- New **"Active monitors"** panel: a row per running session (device label · resolution ·
  transport) with a **Stop** button that stops that session's streamer (its evdi monitor
  disappears). Sessions self-remove on `runningChanged(false)`.
- The **"Start streaming"** button starts a default-port session for the simple case (USB /
  localhost, no client selected) — backward compatible with today's single-tablet flow.

### D. Discovery / wake per session

- WAKE already carries the port (`encode_wake(port)`); USB `usbConnect(serial, port)` already
  takes the port. Each session directs its tablet to its own port — no protocol change.
- mDNS idle-advertising stays a single base-port service; a tablet that dials the base port
  lands on / creates the base session. Rich per-service advertising is out of scope.

### E. Settings

- Per session uses the shared global settings (fps/bitrate/touch/audio/overlay/orientation/
  tls); **resolution is per-tablet native** (from HELLO), so the resolution setting no longer
  drives evdi. Keep the setting as the fallback for a client that reports 0×0.

## Error handling / edges

- Port already bound (stale streamer) → the streamer fails to listen; the GUI surfaces it and
  frees the slot. Allocation skips ports of live sessions.
- A tablet reporting 0×0 → fall back to the settings resolution.
- Stop of one session must not affect others (independent processes, independent evdi devices).
- App close stops every session's streamer (no orphaned root processes).
- Audio: only one session may hold the `droppix-audio` capture; the GUI grants it to the first
  session that enables audio and passes `--audio` only to that one.

## Testing

- **Unit:** `allocate_port` (empty/holes/contiguous), `SessionManager` bookkeeping
  (add/find-by-key/remove/used-ports). The streamer factory keeps the existing test-pattern
  e2e + protocol/daemon tests green.
- **Manual (needs a 2nd tablet):** connect two tablets → two extended monitors at their own
  resolutions; touch on each lands on its own monitor; Stop one leaves the other running.

## Out of scope

- Per-session audio fan-out; per-service mDNS advertising; tablet-initiated multi-session.
- Arranging monitor positions (KWin/Display settings handle placement).
