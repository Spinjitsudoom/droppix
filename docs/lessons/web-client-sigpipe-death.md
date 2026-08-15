# A web client disconnecting killed the whole streamer (SIGPIPE)

**ID:** L-2026-08-15-web-client-sigpipe-death · **Tags:** host, transport, tls, silent-failure, critical · **Severity:** critical

## Symptom

Closing or reloading a browser tab killed `droppix_stream` outright. No log line, no
error — the log just stopped mid-session and every later connection got
connection-refused. Easy to misread as "the client is broken" or "the stream is slow",
because the last thing in the log is a perfectly healthy `stats:` line.

## Root cause

Exit status **141 = 128 + SIGPIPE**. Writing to a socket whose peer has gone away raises
SIGPIPE, whose default action *terminates the process*. The streamer writes to client
sockets constantly, so a tab closing mid-frame killed the server — listener included.

The native path partly escaped it: `SocketChannel` sends with `MSG_NOSIGNAL`. The web
path could not — OpenSSL's `SSL_write` has no equivalent flag, so `WsChannel` was fully
exposed. Nothing else in the process set a SIGPIPE disposition.

## Fix

`droppix::ignore_sigpipe()` (`host/src/signal_setup.h`) called from `stream_main`, next to
the SIGINT/SIGTERM handlers. Those writes then fail with `EPIPE`, which the channels
already treat as an ordinary write failure: `send_all` returns false, the session ends,
and the reconnect loop keeps serving. Ignored dispositions survive fork/exec, so children
inherit the guard.

## How it was found (and the method worth reusing)

Driving the **real** web client against the **real** streamer locally, no phone involved:

```bash
# streamer: synthetic source, no evdi/root/compositor needed
droppix_stream --test-pattern --web --tls --cert c.pem --key k.pem \
               --web-root web/dist --port 27100 --fps 30
# client: headless Chromium via playwright-core against /usr/bin/chromium-browser,
# seeding localStorage settings pre-boot, typing the PIN, sampling __droppixDebug()
```

Two things this setup makes trivial, both of which were needed here:

- **Exit status capture.** The process died silently; running it under a wrapper that
  records `$?` to a file is what turned "it vanished" into "141, therefore SIGPIPE".
- **Pipeline exoneration.** The same harness showed the web path sustaining ~27 fps
  (host `fps 27.2`, client `paint/s=27`) on *both* renderers and with audio on or off —
  which is how a separate low-fps report was localised away from the web stack entirely.

## Gotchas hit while building the harness

- The streamer treats **stdin EOF as shutdown** (the GUI closes stdin to stop it). Launch
  it with stdin held open (`sleep 300 | streamer`, or a FIFO) or it exits ~instantly with
  status 0 and a confusing "listening…" log that stops there.
- `pkill -f 'droppix_stream.*27100'` **matches the running script's own command line** and
  kills the harness mid-run. Use `pkill -f '[d]roppix_stream…'` or `fuser -k <port>/tcp`.
- Playwright's `#pair:not(.show)` never matches a *hidden* element; wait for
  `#pair.show` with `state: "detached"` instead.

## Where

`host/src/signal_setup.h`, `host/src/stream_main.cpp`, `host/tests/test_signal_setup.cpp`.
