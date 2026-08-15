# A slow client link degrades the stream invisibly (bufferbloat)

**ID:** L-2026-08-15-slow-link-bufferbloat · **Tags:** host, transport, performance, silent-failure · **Severity:** high

## Symptom

A web client on real WiFi ran at a few fps while **the host reported perfect health**:
`fps 27.3`, `encode 2.6 ms`, `send 0.1 ms`. Every host-side metric said the stream was
fine. Lowering resolution, switching the client's renderer (WebCodecs→canvas vs MSE), and
removing a canvas filter all changed nothing, because none of them were the constraint.

## Root cause

The encoder runs at a fixed bitrate regardless of what the client's link can carry. When
the link is slower than the stream:

- `SSL_write` does **not** block and does **not** slow down — the kernel socket buffer
  accepts the data immediately. So "time spent sending" stays near zero and looks healthy.
- The excess accumulates in buffers (kernel, and anything between). Latency grows without
  bound, frames arrive later and later, and the effective frame rate collapses.
- The host has no idea. It measures production, not delivery.

**A blocking-send timer cannot detect this.** Instrumenting `send_ms` was a dead end for
exactly this reason — it measured the one thing that stays fast.

## The signal that does work

`ioctl(fd, SIOCOUTQ)` — bytes written but not yet ACKed by the peer. That is the only
host-side quantity that reflects whether the *client* is keeping up. A growing send queue
means we are outrunning the link.

## Fix

Pace capture on that backlog (`host/src/send_backlog.h`): when it exceeds ~250 ms of the
session bitrate, skip captures until it drains to half that (hysteresis stops oscillation).

**Drop frames before the encoder sees them.** A skipped capture leaves the encoder's
reference chain intact — the next frame is simply a delta against the last *encoded* one.
Discarding already-encoded packets instead breaks that chain and corrupts the picture
until the next keyframe (see [delta-drop-corruption.md](delta-drop-corruption.md)). The
stream degrades in frame rate only, never in correctness, and latency stays bounded.

## Measured, over a 2 Mbps link

| | client fps | host reported | backlog |
|---|---|---|---|
| before | 9–10 | 27.3 (wrong) | unbounded |
| after | 15–16 | 12–15 (honest) | bounded 75–99 KB |

New stats fields `backlog_kb_avg` / `link_dropped_per_s` make the condition visible.

## Testing a slow link locally (and how to get it right)

A userspace TCP proxy models a weak link with no root and no phone — but **it must not
read faster than the simulated rate**. The first version read from the server as fast as
it could and queued the excess in userspace: that throttles the client while leaving the
server's socket drained, so the sender never feels backpressure — the exact opposite of a
real slow link, and it made the fix appear not to engage (`backlog 3 KB`).

The faithful version keeps the upstream socket **paused** and pulls only what a token
bucket allows, leaving unread bytes in the kernel so the sender backs up for real. Two
further traps: keep draining after upstream `end` (the server closes after each static
response, so stopping immediately truncates it → `ERR_EMPTY_RESPONSE`), and let the page
load unthrottled before clamping the rate, since a browser's parallel asset fetches
against a single-threaded server are fragile at low bandwidth.

## Where

`host/src/send_backlog.h`, `host/src/socket_pending.h`, `host/src/byte_channel.h`
(`pending_bytes`), `host/src/stream_daemon.cpp`, `host/tests/test_send_backlog.cpp`.
