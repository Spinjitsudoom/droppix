# L-2026-08-16-usb-button-without-a-tunnel: half a transport was removed, and the client kept offering the other half

- **ID:** `L-2026-08-16-usb-button-without-a-tunnel`
- **Tags:** `host`, `gui`, `transport`, `android`, `silent-failure`, `regression`, `high`
- **Date:** 2026-08-16

## Symptom

Tapping **Connect via USB** in the Android client did nothing: no virtual display was ever
created. No error on the host, nothing in the GUI log, and the client just sat there — the
host never saw a connection attempt at all, because none reached it.

Both halves looked healthy in isolation. The streamer was listening (`0.0.0.0:27000`), the
client button was present and wired, and USB tethering and AOA both still worked.

## Root cause

The client's USB button dials `127.0.0.1:27000`, which only reaches the PC through
`adb reverse tcp:27000 tcp:27000`. **Nothing created that tunnel.**

`host/gui/adb_manager.{h,cpp}` — which ran `adb reverse` on Start — was deleted on
2026-07-05 in `a349ee5` ("rewire GUI to tether discovery; remove the adb USB path"). The
Android button was then restored on 2026-07-26 pointing at the same localhost port, with no
host counterpart to make it mean anything.

What made this survive so long is that every *local* signal stayed green:

- `Settings::auto_adb_reverse` was still persisted, still defaulted to `true`, and
  `settings_page.cpp` even commented "always on now (option removed from the GUI); USB just
  works" — but **nothing ever read it**.
- The GUI checkbox was still labelled "USB (adb + tether + AOA)".
- `docs/STATUS.md` listed USB `adb reverse` as **Shipped**.

A grep for `auto_adb_reverse` finds a write and a load and no consumer; that orphaned
setting was the tell.

Confirmed with a controlled test rather than by reading code:

| | `adb reverse --list` | phone → `127.0.0.1:27000` |
|---|---|---|
| As found | empty | `rc=1` (refused) |
| After `adb reverse tcp:27000 tcp:27000` | `tcp:27000 tcp:27000` | `rc=0`, session connects, display appears |

## Fix

`host/gui/adb_reverse.{h,cpp}`: while USB is enabled, poll `adb devices` every 2 s and run
`adb reverse tcp:P tcp:P` for every serial in the `device` state, re-applying on replug.

The tunnel belongs to the adb connection, not to droppix — it does not exist until the
cable is plugged in and it dies on unplug or adb-server restart — so a one-shot at startup
is not enough. Only the `device` state qualifies: a phone on the "Allow USB debugging?"
prompt reports `unauthorized`, and marking it done would skip it forever once the user
tapped Allow (`parse_ready_serials`, tested).

`stop()` deliberately does **not** run `adb reverse --remove`: a session may be streaming
over that tunnel right now, and a stale tunnel is harmless.

## How to detect this in the future

**A persisted setting with no consumer is a deleted feature.** When removing a transport,
grep for its settings key and its client-side entry point in the same change:

```bash
grep -rn "auto_adb_reverse" host/          # writes + loads but no use == orphaned
grep -rn "127.0.0.1\|localhost" android/app/src/main/java   # what the client still dials
```

Also: **a client button is a contract with the host.** Removing a host path requires either
removing the client affordance or replacing the host path. `docs/STATUS.md` saying "Shipped"
was written when the host half existed and was never revisited — a status row is not
evidence that both ends are still connected.
