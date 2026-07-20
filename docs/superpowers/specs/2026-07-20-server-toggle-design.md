# Persistent Server toggle — Design

**Date:** 2026-07-20
**Status:** Approved (design). Not yet implemented.
**Goal:** Replace the GUI's "▶ Start streaming" button with a **persistent on/off Server toggle**: turning it on starts a primary listener session, it **re-arms** after a device disconnects, its state is **saved**, and on the next launch it **restores** (auto-starting the server if it was on).

## Summary

Today `startBtn_` ("▶ Start streaming") calls `onStartStop()`, which each click spawns
a new "Waiting for a tablet…" session on the next free port and never stops — it is a
start-only button. This feature turns it into a checkable **Server** toggle backed by a
persisted marker, controlling a single primary listener session (keyed `server:<port>`)
that keeps listening across device reconnects. Additional monitors still come via the
Connect/discovery flow, unchanged.

## Decisions (locked)

| Question | Decision |
| --- | --- |
| Control | A checkable toggle (reuse `startBtn_`), label `▶ Server: Off` / `■ Server: On`. |
| What it controls | One primary listener session, keyed `server:<port>`. Connect-initiated monitors are untouched. |
| On device disconnect | **Re-arm**: auto-start a fresh waiting session so the server keeps listening. |
| On user turning it off | Stop the session, do not re-arm, remove the marker. |
| Failed-start guard | If the server session dies within `kServerMinRunMs` (2000 ms) and never had a client, flip the toggle **off** and `qWarning` (visible in the F12 console) instead of re-arming. |
| Persistence | Marker file `configDir()/server_enabled` (same existence-based pattern as `minimize_on_close`). |
| Restore on launch | After `restoreLastProfile()`, if the marker exists, defer one event-loop tick then check the toggle on (auto-start). Root via pkexec prompts only if the polkit rule isn't installed. |

## Architecture

### New unit (pure, unit-tested)

`host/gui/server_control.{h,cpp}`:

```cpp
namespace droppix {
inline constexpr qint64 kServerMinRunMs = 2000;
// Re-arm an ended primary-server session iff still enabled AND it did real work
// (had a client at some point, or ran at least kServerMinRunMs — i.e. not a fast failed start).
bool shouldRearm(bool enabled, qint64 elapsedMs, bool everConnected);
}
```

`shouldRearm` is the whole re-arm/failed-start decision, isolated from Qt widgets so it
is testable in `droppix_gui_tests`.

### MainWindow changes

New state:
- `bool serverEnabled_` — the toggle's logical state.
- `QString serverKey_` — key of the live server session (`server:<port>`), empty when none.
- `qint64 serverStartMs_` — start time of the current server session.
- `bool serverEverConnected_` — whether the current server session ever had a client.

New methods (replacing `onStartStop`):
- `void onServerToggled(bool on)` — on: touch marker + `startServerSession()`; off:
  remove marker + `stopServerSession()`. Always `updateServerButton()`.
- `void startServerSession()` — allocate the next free port, set
  `serverKey_ = "server:<port>"`, reset `serverStartMs_`/`serverEverConnected_`, and
  `startSession(serverKey_, "Server — waiting for a device…", "", port, "", {})`. If no
  port is free, `qWarning` and revert the toggle.
- `void stopServerSession()` — stop the `serverKey_` session (teardown handled by the
  existing `runningChanged(false)` path).
- `void updateServerButton()` — set the toggle's text/checked visual from `serverEnabled_`.

Wiring in `wireSession()`:
- `statsReceived`: when `key == serverKey_` and `client_connected`, set
  `serverEverConnected_ = true`.
- `runningChanged(false)` (after the existing teardown): when `key == serverKey_`, clear
  `serverKey_`, compute `elapsed = now - serverStartMs_`, and
  - if `shouldRearm(serverEnabled_, elapsed, serverEverConnected_)` → defer
    (`QTimer::singleShot(0, …)`) then `startServerSession()` if still enabled;
  - else if `serverEnabled_` (enabled but a fast failure) → set `serverEnabled_=false`,
    remove marker, update the toggle (with `QSignalBlocker` so it doesn't re-enter
    `onServerToggled`), and `qWarning(...)`.

Constructor:
- Make `startBtn_` checkable; connect `toggled(bool)` → `onServerToggled`.
- After `restoreLastProfile()`: if `configDir()/server_enabled` exists,
  `QTimer::singleShot(0, this, [this]{ startBtn_->setChecked(true); });` so the window
  paints before the pkexec path runs.

## Data flow

```
toggle ON ─► onServerToggled(true) ─► touch marker ─► startServerSession()
                                                          │ startSession("server:<port>", …)
device connects ─► statsReceived(client_connected) ─► serverEverConnected_ = true
device drops ─► runningChanged(false) ─► shouldRearm? ── yes ─► startServerSession() (re-arm)
                                                       └─ no + enabled ─► toggle OFF + qWarning
toggle OFF ─► onServerToggled(false) ─► remove marker ─► stopServerSession() (no re-arm)
launch + marker present ─► setChecked(true) ─► onServerToggled(true) ─► auto-start
```

## Error handling

- Fast failed start (pkexec denied, port clash, missing streamer): guarded by
  `shouldRearm` → toggle flips off + warning, no re-arm loop.
- Monitor limit reached on start/re-arm: `qWarning` and leave the toggle off.
- Turning the toggle off mid-session: `serverEnabled_` is false before `stop()`, so the
  `runningChanged` handler neither re-arms nor warns.

## Testing

- `host/gui/tests/test_server_control.cpp` (in `droppix_gui_tests`):
  - disabled → never re-arms (any elapsed/everConnected).
  - enabled + everConnected → re-arms (even for short sessions).
  - enabled + `elapsed >= kServerMinRunMs`, never connected → re-arms.
  - enabled + fast (`< kServerMinRunMs`) + never connected → does not re-arm.
- Manual: toggle on → server waits; connect a device → streams; disconnect → server
  re-arms (still waiting); toggle off → stops; relaunch → auto-starts. Deny pkexec once →
  toggle flips off with a console warning, no prompt storm.

## Files

New:
- `host/gui/server_control.h`, `host/gui/server_control.cpp`
- `host/gui/tests/test_server_control.cpp`

Changed:
- `host/gui/main_window.h` — replace `onStartStop` decl with the new methods + state.
- `host/gui/main_window.cpp` — toggle UI, `onServerToggled`/`startServerSession`/
  `stopServerSession`/`updateServerButton`, `wireSession` re-arm + connect tracking,
  restore-on-launch.
- `host/CMakeLists.txt` — add `gui/server_control.cpp` to `droppix_gui`; add the test +
  source to `droppix_gui_tests`.

## Non-goals

- Multiple simultaneous auto-waiting listeners (the toggle is one primary server).
- Changing the Connect/discovery flow or the Active-monitors panel.
- Per-device server persistence beyond the single on/off marker.
