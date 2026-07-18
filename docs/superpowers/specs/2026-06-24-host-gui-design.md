# droppix Host GUI — Design

**Date:** 2026-06-24
**Status:** Shipped on master.
**Scope:** The Linux host control GUI only. The Android client GUI is a separate
spec/plan to follow.

## Goal

Replace the `droppix_stream` command line (and the manual `adb reverse` / `sudo`
steps) with a Qt desktop control panel: pick the source and quality, save named
profiles, start/stop streaming with one button, and see live connection status,
latency stats, and a log — without touching a terminal.

## Decisions (settled during brainstorming)

| Decision | Choice | Rationale |
|---|---|---|
| Toolkit | **Qt 6 Widgets / C++** | Native KDE look; same language as the host engine; Qt6 runtime already on the host (Plasma); build in the `droppix-dev` distrobox, run natively on the host |
| Relationship to engine | **Supervise** the `droppix_stream` process | Keeps the CLI as the engine; confines root to the streamer subprocess; lower coupling |
| Privilege handling | Launch streamer directly for test-pattern; via **`pkexec`** for the evdi (real-monitor) source | Root stays in the streamer process; the GUI runs unprivileged |
| Stats feed | New **`--stats-json`** flag on `droppix_stream` | Parsing structured JSON is robust vs scraping text |
| Scope/order | **Host GUI first**, Android client GUI later (separate spec) | Two independent GUIs |
| Purpose | Replace CLI/adb **plus richer settings** (profiles, source toggle, presets, log view) | User's chosen scope |

## Engine change required (small, additive)

Add a `--stats-json` flag to `droppix_stream`. When set, instead of the human
`stats:` line it prints **one JSON object per second** to stderr:

```json
{"encode_ms_avg":4.2,"encode_ms_peak":7.1,"fps":30.0,"frame_kb_avg":36.0,"frame_kb_peak":74.5,"client_connected":true}
```

`client_connected` reflects whether a client has completed the HELLO handshake.
The default (no flag) keeps the existing human-readable `stats:` line. This is the
GUI's data feed; both ends of the format are owned by this project.

## Architecture

```
        ┌──────────────────────── droppix-gui (Qt6, unprivileged) ───────────────────────┐
        │  MainWindow (control panel)                                                     │
        │     │ settings                ▲ stats/status/log signals                        │
        │     ▼                         │                                                 │
        │  ArgsBuilder (settings→argv)  StatsParser (json→Stats)   ProfileStore (json IO) │
        │     │                         ▲                                                 │
        │     ▼                         │ stderr lines                                    │
        │  StreamController ── QProcess ─┴─► droppix_stream  (direct, or via pkexec for evdi)
        │  AdbManager ── QProcess ─► adb devices / adb reverse tcp:PORT tcp:PORT          │
        └─────────────────────────────────────────────────────────────────────────────────┘
```

### Components (each one focused, with a clear interface)

| Component | Responsibility | Tested |
|---|---|---|
| `MainWindow` | The Qt Widgets control panel: settings widgets, profile dropdown + Save/Save As/Delete, Start/Stop button, device + stream status, stats line, scrolling log pane. Wires the others together via signals/slots. | manual |
| `StreamController` | Owns a `QProcess` running `droppix_stream`. Builds argv via `ArgsBuilder`; prefixes `pkexec` when the source is evdi. Streams stderr lines out as signals (raw log + parsed stats). Kills the process on Stop and on app exit; reflects crash/exit in status. | manual + integration |
| `AdbManager` | `QProcess` wrappers: poll `adb devices` (report device serial + state: device/unauthorized/none), and run `adb reverse tcp:PORT tcp:PORT` on Start. Handles `adb` missing gracefully. | manual |
| `StatsParser` | **Pure.** Parse one `--stats-json` line into a `Stats { double encodeMsAvg, encodeMsPeak, fps, frameKbAvg, frameKbPeak; bool clientConnected; bool valid; }`. Malformed input → `valid=false`, never throws. | unit |
| `ArgsBuilder` | **Pure.** Map a `Settings` struct to the exact `droppix_stream` argv (source flag, `--width/--height/--fps/--bitrate/--port`, `--stats-json`), and decide whether `pkexec` is needed and whether to run `adb reverse`. | unit |
| `ProfileStore` | **Pure-ish.** Load/save named `Settings` profiles as JSON under `~/.config/droppix/profiles.json`; provide a built-in default; round-trip safely. | unit |

### Data model

```
Settings {
  enum Source { TestPattern, Evdi };
  Source source;
  int width, height;   // test-pattern only; for evdi the streamer is fixed 1080p
  int fps;
  int bitrateKbps;
  int port;            // default 27000
  bool autoAdbReverse; // default true
}
```

## Settings & UI

- **Source:** Test pattern · Real monitor (evdi).
- **Resolution:** presets (640×480 / 1280×720 / 1920×1080) for the test-pattern
  source. For evdi the control is shown disabled with a note ("evdi is fixed at
  1080p; encode-downscale is a planned engine feature") — the streamer currently
  encodes the evdi capture at its native 1080p.
- **FPS**, **Bitrate (kbps)**, **Port**.
- **Auto `adb reverse` on Start** (checkbox, default on).
- **Profiles:** dropdown + Save / Save As / Delete, persisted to
  `~/.config/droppix/profiles.json`.
- **Status:** device (serial + connected/unauthorized/none), stream
  (Stopped / Running / Running — client connected), and a live stats line
  (encode avg/peak ms · fps · frame avg/peak KB).
- **Start/Stop:** one button that toggles; becomes Stop while running.
- **Log pane:** scrolling view of the streamer's stderr (the human log lines,
  with the JSON stats line consumed by the parser, not dumped raw).

Layout (control panel, top-to-bottom): profile row → settings group →
status group → Start/Stop button → log pane.

## Privilege model

- GUI process: unprivileged.
- Test-pattern source: `StreamController` runs `droppix_stream …` directly.
- evdi source: runs `pkexec droppix_stream …` → graphical auth prompt; root is
  confined to the streamer subprocess. A polkit rule for prompt-free UX is a
  later polish, out of scope for v1.

## Error handling

- `adb` missing/not running → status shows "adb not found"; Start still works for
  test-pattern (no tunnel needed) and warns for evdi-over-USB.
- Streamer fails to start / exits unexpectedly / `pkexec` denied → status returns
  to Stopped with the error surfaced in the log; the Start button resets.
- Malformed/absent stats lines → parser returns `valid=false`; the stats line
  shows "—" rather than stale values.
- GUI exit while streaming → the supervised process is terminated cleanly.

## Testing strategy

- **Unit (GoogleTest, reusing the host CMake/test setup):**
  - `StatsParser`: valid JSON → correct fields; missing/extra fields and garbage
    → `valid=false` without throwing.
  - `ArgsBuilder`: each `Settings` combination → exact expected argv; evdi →
    `pkexec` prefix present; test-pattern → absent; `--stats-json` always present;
    `autoAdbReverse` decision correct.
  - `ProfileStore`: save then load returns identical `Settings`; unknown file →
    default profile; corrupt file → safe fallback.
- **Integration/manual:** build the app; run it; start a test-pattern stream and
  confirm live stats + log; start the evdi source and confirm the `pkexec` prompt
  and a real stream; confirm `adb devices` detection and auto-reverse; confirm
  clean teardown on Stop and on window close.

## Build & run

- Built in the `droppix-dev` distrobox with Qt6 dev packages, off the CIFS mount
  (same constraint as the rest of the host code: no exec bit in-tree).
- Runs natively on the host (Plasma's Qt6 runtime). The scaffold task verifies
  the container Qt6 version is compatible with the host runtime.
- New code lives under `host/gui/` with its own CMake target `droppix_gui`,
  linking the pure helpers (which may live in `host/gui/` or reuse `host/src`).

## Out of scope (this spec)

- The Android client GUI (separate spec/plan, next).
- Encode-downscale for the evdi source (separate latency-tuning engine feature;
  the GUI will expose its resolution control for evdi once it exists).
- WiFi transport / pairing UI (later project phase).
- A polkit rule for prompt-free evdi start (later polish).
- System-tray / background operation (the window is the app for v1).
