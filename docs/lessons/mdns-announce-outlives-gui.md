# L-2026-11-24-mdns-announce-outlives-gui: every killed GUI left its mDNS announcement running

- **ID:** `L-2026-11-24-mdns-announce-outlives-gui`
- **Tags:** `gui`, `transport`, `silent-failure`, `medium`, `lesson`
- **Date:** 2026-11-24

## Symptom

Ten `avahi-publish-service ... _droppix._tcp 27000` processes were running with one GUI
open. Tablets could see stale duplicate "(droppix)" hosts.

## Root cause

`MdnsAdvertiser` stopped its `QProcess` in `stop()` / the destructor. Neither runs when the
GUI is killed (logout, `kill`, a crash, a restart during testing), so the child was
re-parented to `systemd --user` and announced forever. The GUI also had no SIGTERM handler,
so a logout skipped the tray-Quit shutdown entirely.

## Fix

- `endWithParent(QProcess&)` (`gui/process_lifetime.*`) sets `PR_SET_PDEATHSIG = SIGTERM` in
  the child, and exits at once if the parent already died. Used by the advertiser and browser.
  It does not work for the pkexec streamer: the kernel clears it for setuid programs. The
  streamer ends on stdin EOF instead.
- `QuitSignals` turns SIGTERM / SIGINT / SIGHUP into `MainWindow::quitNow()`, the same path
  as the tray's Quit.

## How to detect this in the future

`pgrep -af avahi-publish-service` should list exactly one per running GUI, with that GUI
as its parent (`ps -o ppid=`). Any long-running helper the GUI spawns needs `endWithParent`.
