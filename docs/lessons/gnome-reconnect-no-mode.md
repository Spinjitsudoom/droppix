# L-2026-11-25-gnome-reconnect-no-mode: GNOME kept a reconnected droppix monitor "on" and never moded it

- **ID:** `L-2026-11-25-gnome-reconnect-no-mode`
- **Tags:** `host`, `evdi`, `gnome`, `desktop-backend`, `silent-failure`, `high`, `lesson`
- **Date:** 2026-11-25

## Symptom

On GNOME 46 (Ubuntu 24.04), the first session after boot got a mode. Every quick reconnect
after it (a new session a few seconds later, which is also what orientation restarts do) logged
`evdi: no compositor mode within 12s` and failed. Another session after that sometimes got a mode
but `could not identify the droppix output`.

## Root cause

- **No mode.** On reconnect, Mutter's `GetCurrentState` already listed the droppix monitor as
  enabled at its old position, so Mutter saw nothing to change and committed no mode to the new
  evdi connection. The evdi kernel log showed connect + EDID set and then nothing.
- **Wrong identification.** The previous session's monitor was still listed when this session
  snapshotted "outputs before", so the "newly appeared output" rule found nothing new.

## Fix

- `FrameSource::start` takes an `on_stalled` callback. If no mode arrives within 3 s, the evdi
  source calls it on a background thread and keeps waiting. `StreamDaemon` points it at
  `DesktopBackend::revive_output(edid_serial)`.
- `GnomeBackend::revive_output` applies (as temporary configs) the layout without the droppix
  monitor, then the old layout again. Mutter commits a mode within a second.
- The output is now found by its EDID serial first (`DesktopBackend::find_output`, which uses
  `mutter_find_by_serial`). The serial is the session port and is unique per session. Mutter
  prints it as hex (`0x00006978`), so the match accepts hex as well as decimal.

## How to detect this in the future

Run several sessions back to back (`~/droppix-vms/spike/cycle.sh` in the GNOME VM). Every
session must log `mode changed`. `revive (off)/(on) applied` lines show the nudge firing.
