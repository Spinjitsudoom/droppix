# In-GUI Log Console (host debug panel) — Design

**Date:** 2026-07-18
**Status:** Shipped on master (2026-07-21). Implemented as `host/gui/log_{entry,classify,buffer,forwarder,model,panel}.*` with 11 unit tests; F12 dock in MainWindow.
**Goal:** Surface everything droppix currently prints to the terminal — the streamer subprocess output **and** the GUI's own Qt log messages — inside a searchable, filterable dock panel in `droppix_gui`, so debugging no longer requires reading the terminal/journald.

## Summary

`droppix_gui` already captures the (root, pkexec-spawned) streamer's merged
stdout/stderr in `StreamController` and emits a `logLine` signal per
non-structured line. Today those lines are forwarded to `qInfo(...)`
([host/gui/main_window.cpp:749](../../../host/gui/main_window.cpp)), i.e. dumped
to the terminal, and the GUI's own `qWarning`/`qCritical` calls go there too.

This feature adds a **central in-memory log sink** plus a **bottom dock panel**
that shows those lines live, with search / level / source / session filtering,
autoscroll, copy, and save-to-file. Terminal/journald output is preserved
(the panel is additive, not a replacement for logging).

MVP is a usable single-screen console: capture always-on, merged multi-session
stream, tag by source and session, filter and search, save to file.

## Non-goals

- Web-client (browser console) or Android-client log aggregation — the panel
  covers host-side (streamer + GUI) only. Cross-process log transport is a
  separate future design.
- A telemetry/stats dashboard (fps/bitrate/latency charts). `statsReceived` is
  intentionally **not** routed to this console; that is a separate feature.
- Regex search, log-level configuration/verbosity control of the streamer, or
  persistent log storage across app restarts.
- Replacing `qInstallMessageHandler`'s existing terminal/journald output.

## Decisions (locked)

| Question | Decision |
| --- | --- |
| What is it? | In-GUI log console, a `QDockWidget` docked at the bottom of the main window. |
| Sources | Streamer `logLine` + connection/approval events **and** the GUI's own Qt messages (`qInfo`/`qWarning`/`qCritical`/`qFatal`) via `qInstallMessageHandler`. |
| Multi-session | One merged stream; each entry tagged with its session key (e.g. `mon1`); filterable by session. |
| Terminal output | **Kept.** The message-handler shim chains to the previous handler so journald/terminal still receive everything. |
| Search | Case-insensitive substring (no regex for MVP). |
| Capacity | Ring buffer capped at 5000 entries; oldest dropped. |
| Always capturing | Yes — the buffer fills even while the panel is hidden, so post-hoc inspection works. |
| Save | `Save…` writes the **full** buffer (not the filtered view) to a `.log` file. |
| Toggle | View menu item + `F12` + a toolbar button. |

## Architecture

```
streamer stdout/stderr ─(existing)─► StreamController::logLine ─┐
connection/approval events ─────────► (as INFO events) ─────────┤
                                                                 ├─► LogBuffer ─► LogPanel (dock)
GUI qInfo/qWarning/qCritical ─► log_forwarder (msg handler) ─────┘   ring 5000    filter + QListView
                                        └─(chain)─► previous handler ► terminal / journald (kept)
```

The design is split into small units that can be understood and tested
independently. Only `LogPanel` touches widgets; all classification and
buffering logic is Qt-widget-free and unit-tested.

### Units

**`LogEntry`** (`host/gui/log_entry.h`) — plain value struct:

```cpp
enum class LogLevel { Info, Warn, Error };
struct LogEntry {
  qint64  epochMs;    // GUI receipt time (QDateTime::currentMSecsSinceEpoch)
  QString session;    // session key, e.g. "mon1"; empty for GUI-global messages
  QString source;     // e.g. "tls", "web", "enc", "gui"; may be empty
  LogLevel level;
  QString text;       // message body (tag stripped from source when parsed)
};
```

**`classifyStreamerLine`** (`host/gui/log_classify.{h,cpp}`) — pure function,
no widgets, no I/O:

```cpp
struct Classified { QString source; LogLevel level; QString text; };
Classified classifyStreamerLine(const QString& raw);
```

- `source` = the leading `tag:` token when the line matches `^[a-z0-9_-]+:`
  (e.g. `tls: SSL_accept failed` → source `tls`, text `SSL_accept failed`).
  When there is no such prefix, `source` is empty and `text` is the whole line.
- `level` heuristic (case-insensitive): contains `fail`, `error`, `errno`,
  `refused`, `cannot`, or `unable` → `Error`; contains `warn`, `retry`, or
  `deprecated` → `Warn`; otherwise `Info`.
- The heuristic is best-effort and non-authoritative; the raw text is always
  preserved so nothing is lost to misclassification.

**`LogBuffer`** (`host/gui/log_buffer.{h,cpp}`, `QObject`) — the single sink:

- `void append(const LogEntry&)` — appends; drops the oldest when size exceeds
  `kCap` (5000).
- `signals: void entryAdded(const LogEntry&)` and `void cleared()`.
- `const std::deque<LogEntry>& entries() const` — for backfill when the panel is
  first shown and for `Save…`.
- `void clear()`.
- GUI-thread affinity: `append` is only ever called on the GUI thread (the
  forwarder marshals cross-thread messages — see below).

**`log_forwarder`** (`host/gui/log_forwarder.{h,cpp}`) — installs the Qt message
handler at startup:

- `void installLogForwarder(LogBuffer* buffer);`
- Maps `QtMsgType` → `LogLevel` (`QtDebugMsg`/`QtInfoMsg`→Info,
  `QtWarningMsg`→Warn, `QtCriticalMsg`/`QtFatalMsg`→Error), source `"gui"`.
- Because Qt messages may originate on any thread, the handler posts the entry
  to the `LogBuffer` via a queued connection / `QMetaObject::invokeMethod(...,
  Qt::QueuedConnection)` so `append` runs on the GUI thread.
- **Chains** the previously installed handler (captured at install time) so the
  default terminal/journald output is preserved.

**`LogPanel`** (`host/gui/log_panel.{h,cpp}`, `QDockWidget`) — the only widget
unit:

- Backfills from `buffer.entries()` on construction, then appends on
  `entryAdded`.
- View: `QListView` over a lightweight model reading the buffer, with a filter
  predicate combining substring search + level toggles + source/session
  selection. Per-level foreground color (Error red, Warn amber, Info default).
- Toolbar: search `QLineEdit`; level toggle buttons (Info/Warn/Error);
  source/session `QComboBox` (populated from tags seen so far); `Autoscroll`
  toggle; `Clear`; `Copy` (selected rows → clipboard); `Save…`
  (`QFileDialog` → full buffer to `.log`).
- Autoscroll follows the tail, auto-pauses when the user scrolls up, resumes
  when scrolled back to the bottom.

### Wiring (`host/gui/main.cpp`, `host/gui/main_window.cpp`)

- Construct one app-wide `LogBuffer`. Call `installLogForwarder(&buffer)` early
  in `main()` (after `QApplication`).
- Construct the `LogPanel` bound to the buffer; add it as a bottom
  `QDockWidget`; add a View-menu action + `F12` shortcut + toolbar button to
  toggle it.
- Replace the `qInfo("%s", ...)` forwarding at
  [main_window.cpp:749](../../../host/gui/main_window.cpp) with
  `buffer.append(classifyStreamerLine(line) + session tag)`. Do the same for
  every per-session `StreamController` created in `session_manager` — connect
  each `logLine` to append with that session's key.
- Route `connecting` / `approvalRequested` events into the buffer as `Info`
  entries as well, so the console shows the full connection lifecycle.

## Data flow

1. Streamer prints a line → `StreamController::onReadyRead` splits it → emits
   `logLine(line)` (structured lines already peeled off as stats/approve/connect).
2. `main_window` slot runs `classifyStreamerLine`, stamps session + time, calls
   `buffer.append`.
3. GUI code calls `qWarning(...)` → forwarder maps + marshals to GUI thread →
   `buffer.append`; previous handler also prints to terminal.
4. `buffer` emits `entryAdded` → `LogPanel` appends to its model → view updates
   (autoscroll if at tail) subject to the active filter.

## Error handling

- Message handler must never throw or re-enter the buffer during a fatal
  message; `qFatal` still aborts after being recorded and passed to the chained
  handler.
- Buffer overflow is normal operation (ring drop), not an error.
- `Save…` I/O failure shows a non-modal status message; it never crashes the
  session.
- High log volume: appends are coalesced by Qt's event loop; the ring cap bounds
  memory. If profiling later shows UI stalls, batch view updates on a short
  timer — deferred, not in MVP.

## Testing

Matches the repo's gtest culture; logic units are widget-free where possible.

Both logic tests live under the `droppix_gui_tests` target because
`classifyStreamerLine` and `LogBuffer` use `QString`/`QObject`.

- **`test_log_classify.cpp`** (in `droppix_gui_tests`) — table of real sample
  lines:
  - `tls: SSL_accept failed` → source `tls`, level `Error`.
  - `web: websocket client from 192.168.1.5` → source `web`, level `Info`.
  - `vaapi: low_power entrypoint retry` → source `vaapi`, level `Warn`.
  - `starting encoder` (no colon) → source empty, level `Info`, text unchanged.
- **`droppix_gui_tests/test_log_buffer.cpp`** — ring cap enforcement (append
  `kCap + N`, expect size `kCap`, oldest dropped, newest kept), ordering, and
  `entryAdded`/`cleared` emissions via `QSignalSpy`.
- `LogPanel` widget is intentionally logic-light (all logic in
  buffer/classify), so it has no fragile UI unit test. Manual check: toggle
  with `F12`, trigger a TLS/pkexec error, confirm it appears with the right
  level color and is filterable/searchable; confirm terminal still shows it.

## Files

New:
- `host/gui/log_entry.h`
- `host/gui/log_classify.{h,cpp}`
- `host/gui/log_buffer.{h,cpp}`
- `host/gui/log_forwarder.{h,cpp}`
- `host/gui/log_panel.{h,cpp}`
- `test_log_classify.cpp` and `test_log_buffer.cpp`, both under the
  `droppix_gui_tests` target

Changed:
- `host/gui/main.cpp` — install forwarder, construct buffer.
- `host/gui/main_window.{h,cpp}` — panel, toggle action/shortcut, reroute
  `logLine`/events into the buffer.
- `host/gui/session_manager.*` — per-session `logLine` wiring (session tag).
- `host/CMakeLists.txt` — add new sources to `droppix_gui` and the tests to
  `droppix_gui_tests`.

## Phased delivery (for the plan)

1. `LogEntry` + `classifyStreamerLine` + tests.
2. `LogBuffer` + tests.
3. `log_forwarder` (message handler + chain + thread marshaling).
4. `LogPanel` dock (view/model/filter/toolbar) + toggle wiring.
5. Rewire streamer `logLine` + events into the buffer, per session.
6. Manual E2E: force a TLS/pkexec error, verify capture, filtering, save;
   confirm terminal/journald output unchanged.
