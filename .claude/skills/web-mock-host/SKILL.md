---
name: web-mock-host
description: >-
  Run the local HTTPS/WSS mock droppix host to debug the web PWA client
  (video, audio, clicks, fullscreen) without Linux/evdi. Use when the user
  asks to mock the host, test the web client locally, or debug WSS/input.
---

# web-mock-host

Tool: [`tools/web-mock-host/`](../../../tools/web-mock-host/README.md)

## When to use

- Debug `web/` on macOS without a real `droppix_stream` host.
- Verify Connect → CONFIG → WebCodecs video, PCM audio, fit/fullscreen, and input wire (Touch/Mouse/Scroll/Key).

## Steps

1. Ensure web dist exists: `cd web && npm ci && npm run build`
2. Ensure `ffmpeg` is on PATH.
3. Start mock:

```bash
cd tools/web-mock-host
npm install
npm start
```

4. Open `https://localhost:8443/` in Chromium, trust the cert. Mock auto-Connects; unmute for MP4 audio.
5. Video+audio come from a looped MP4 (`assets/sample.mp4` or `DROPPIX_MOCK_MP4`) for lipsync checks.

## Env overrides

- `PORT` (default 8443)
- `PAIRING_CODE` (default 123456)
- `DROPPIX_WEB_ROOT` (default repo `web/dist`)
- `DROPPIX_MOCK_MP4` (path to any `.mp4`)

## Playwright E2E

```bash
cd tools/web-mock-host
npx playwright install chromium   # once
npm run test:e2e
```

Asserts PIN, Connect, decoded canvas pixels, input kinds via `/debug/inputs`, fit/mute, Disconnect.

## Do not

- Treat this as a substitute for Chromium LAN E2E against real `--web` streamer.
- Commit `certs/` or `node_modules/`.
- Expect Cursor Playwright MCP to open `https://localhost` with the self-signed cert (use `npm run test:e2e` instead).
