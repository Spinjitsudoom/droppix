# L-2026-07-18-ghost-ws-stream: video keeps playing after disconnect (orphaned WebSocket)

- **ID:** `L-2026-07-18-ghost-ws-stream`
- **Tags:** `client`, `transport`, `silent-failure`, `high`, `lesson`
- **Date:** 2026-07-18
- **Related:** `L-2026-07-18-delta-drop-corruption`

## Symptom

After clicking Disconnect in the web PWA, the movie kept playing on the canvas. Looked like "multiple streams" being sent.

## Root cause

Two stacked bugs in `web/src/`:

1. `main.ts` `connect()` had no single-flight guard: the mock auto-connect timer and a manual Connect click could each call `wireTransport()`, and the second `Transport` overwrote the first. The orphaned first socket stayed open with live `onmessage`/`onVideo` handlers painting into the same shared canvas. Disconnect only closed the tracked transport.
2. `transport.ts` handlers (`onmessage`, `onclose`, `onopen`) did not check whether their socket was still the active one (`this.ws !== ws`), so a replaced/closed socket kept delivering frames.

Server side compounded it: the mock spawned one ffmpeg per WSS connection with no session limit, so each ghost socket had its own live stream.

## Fix

- `transport.ts`: every socket callback bails when `this.ws !== ws`; `close()` detaches `this.ws` before sending BYE.
- `main.ts`: `connecting` single-flight flag + `if (transport) return` in `connect()`; `wireTransport()` closes any previous transport; `onClose` nulls `transport`; `InputBinder` created once (`input ??=`) and sends via `transport?.send`.
- `server.mjs`: single `activeSession`; a new HELLO preempts the previous session (stops its ffmpeg, closes its socket). `/debug/session` exposes `{active}` for tests.

## How to detect this in the future

- Playwright regression in `tools/web-mock-host/e2e/web-client.spec.mjs`: after Disconnect, canvas must be black, must STAY black 1.5s later, and `/debug/session` must report `active: false`.
- Log line `preempting previous session` in the mock host means a client opened two HELLOs - investigate the client if it appears outside deliberate reconnects.
