# L-2026-11-24-vanished-tablet-held-session: a tablet that dropped off Wi-Fi held its session for minutes

- **ID:** `L-2026-11-24-vanished-tablet-held-session`
- **Tags:** `host`, `transport`, `silent-failure`, `high`, `lesson`
- **Date:** 2026-11-24

## Symptom

(Found in review.) A tablet that drops off Wi-Fi sends no FIN. The host kept "streaming"
to it, and the tablet could not reconnect.

## Root cause

- No dead-peer detection: Linux retries unacknowledged data for about 15 minutes.
- The link-pacing check (`should_skip_frame` on `SIOCOUTQ`) stopped sending once the
  backlog filled, so no send ever failed.
- A static screen sends nothing at all.
- The listener is one client at a time and is not polled during a session, so the
  reconnecting tablet waited in the accept backlog.

## Fix

`enable_dead_peer_detection(fd)` (`src/socket_pending.h`), called on every accepted
client socket: native, web and spacedesk-compat.
- Keep-alives: 5 s idle, then 3 probes 2 s apart.
- `TCP_USER_TIMEOUT` = 10 s.

The dead socket then errors in `poll_control()` / `send_all()`, and the session ends.

## How to detect this in the future

Any accepted TCP socket that can sit idle, or that is paced on its backlog, needs
keep-alives and `TCP_USER_TIMEOUT`. Test: `SocketChannel.DeadPeerDetectionIsArmedOnTcp`.
