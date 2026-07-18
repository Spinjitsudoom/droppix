# droppix web PWA (host-served)

Browser / installable PWA client served by `droppix_stream` over HTTPS on the session port. Connects with same-origin WSS and speaks protocol v5 message bodies (WebSocket frames are `[type][body]` without the TCP length prefix).

## Build

```bash
cd web
npm ci
npm run build    # → web/dist/
npm test
```

Set `DROPPIX_WEB_ROOT` to the absolute path of `web/dist` if the GUI cannot find it automatically.

## Use

1. Build `web/dist` (or use a packaged AppImage/Flatpak that embeds it).
2. In the host GUI Settings, enable **Offer web / PWA client**.
3. Start streaming. The Active monitors panel shows `https://<lan-ip>:<port>/` and a QR code.
4. On the client device, open the URL, accept the self-signed certificate warning, confirm the PIN matches the PC, then **Connect**.
5. Optional: Install as a PWA (Chromium) or Add to Home Screen (iOS).

## Features (MVP)

- H.264 via WebCodecs → canvas (contain / cover / stretch)
- PCM audio (48 kHz s16le stereo) via AudioWorklet
- Touch / mouse / scroll / keyboard → host uinput
- Fullscreen API + `display: fullscreen` manifest
- Service worker caches the shell only (never `/ws`)

## Local mock (no Linux host)

On macOS (or without `droppix_stream`), use the mock host - it loops a real MP4 (synced A/V) over WSS:

```bash
cd tools/web-mock-host
npm install
npm start
# → https://localhost:8443/  PIN 123456
# optional: DROPPIX_MOCK_MP4=/path/to/clip.mp4 npm start
```

See [`tools/web-mock-host/README.md`](../tools/web-mock-host/README.md). Automated: `cd tools/web-mock-host && npm run test:e2e`.

## Limits

- Chromium-stable is the primary target; Firefox H.264 WebCodecs is best-effort.
- No USB / AOA / adb / mDNS from the browser.
- Self-signed TLS requires a one-time browser trust step on the LAN.
