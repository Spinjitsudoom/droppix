# G-2026-08-16-popen-inherits-sockets: `popen()` handed `parec` the listening socket

- **ID:** `G-2026-08-16-popen-inherits-sockets`
- **Tags:** `host`, `transport`, `audio`, `silent-failure`, `gotcha`, `medium`
- **Date:** 2026-08-16
- **Related:** `web-client-sigpipe-death` (the other way a child/peer takes the streamer down)

## Symptom

`ss -tnp` reported the streaming connection as owned by **`parec`**, not `droppix_stream`:

```
ESTAB  127.0.0.1:27000  127.0.0.1:37163  users:(("parec",pid=398243,fd=6))
```

`parec` is the PipeWire/PulseAudio capture helper. It has no business holding a client
socket, and it held the **listening** socket too.

## Root cause

`AudioStreamer` starts capture with `::popen("parec …", "r")` (`audio_streamer.cpp:15`).
`popen` forks, and **fork hands the child every descriptor that is not marked
close-on-exec**. Nothing in the codebase set `FD_CLOEXEC`:

```
$ grep -rn "SOCK_CLOEXEC\|FD_CLOEXEC" host/src/
(no matches)
```

So `parec` — plus the `xrandr`/`kscreen-doctor` children in `desktop_backend.cpp` — inherited
the listening socket and every accepted client socket.

Two consequences, and the first is the nasty one:

1. **The port stays bound by an invisible owner.** `parec` lives for the whole audio session,
   and if the streamer is killed rather than shut down cleanly, `parec` is orphaned still
   holding the listening socket. The next start fails to bind — while `ps` shows no
   `droppix_stream` at all, so the port looks held by nothing. `SO_REUSEADDR` does **not**
   rescue this: it covers `TIME_WAIT`, not a live process holding the socket.
2. **Clients don't see a clean disconnect.** The streamer closing an accepted socket does not
   release it while a child still holds a copy, so no FIN is sent until `parec` exits.

## Fix

Create sockets non-inheritable, atomically:

```cpp
listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
int fd = ::accept4(listen_fd_, (sockaddr*)&cli, &cli_len, SOCK_CLOEXEC);
```

Applied at every socket site: `transport_server.cpp` (listen + accept), `web_frontend.cpp`
(accept), `spacedesk_server.cpp` (TCP listen, UDP discovery, accept).

Two things worth knowing:

- **`accept()` does not inherit the listening socket's `FD_CLOEXEC`.** Setting it on the
  listener is not enough; the accepted fd needs its own, which is why `accept4` exists.
- **Set it at creation, not afterwards with `fcntl`.** A create-then-`fcntl` pair leaves a
  window in which another thread's `popen` still inherits the descriptor.

## How to detect this in the future

Any process that calls `popen`/`fork`+`exec` while holding sockets needs this. The check is
one command — if a helper appears here, a descriptor escaped:

```bash
ss -tnp | grep -v droppix_stream      # who really owns the streamer's sockets?
ls -l /proc/$(pgrep -x parec)/fd | grep socket
```

Regression test: `host/tests/test_socket_cloexec.cpp` asserts `FD_CLOEXEC` on
`TransportServer::listen_fd()`, including after a re-`listen()`.
