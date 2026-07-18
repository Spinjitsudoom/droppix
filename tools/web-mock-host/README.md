# web-mock-host

Local HTTPS/WSS mock of `droppix_stream` for the web PWA. Loops a real **MP4** (video + audio kept in sync) so you can lipsync-check timing, and logs/echoes input via `/debug/server-marks`.

## Prerequisites

- Node 20+
- `ffmpeg` + `ffprobe` + `curl` on `PATH`
- Built web client: `cd web && npm ci && npm run build`

## Quick start

```bash
cd tools/web-mock-host
npm install
# fetches assets/sample.mp4 if missing: Tears of Steel dialogue segment
# (CC-BY blender.org) → Sintel trailer → local test clip, in that order
npm start
# → https://localhost:8443/   PIN 123456
```

Use **Chromium**. Connect (mock auto-checks PIN). Unmute for MP4 audio / lipsync.

### Your own MP4

```bash
DROPPIX_MOCK_MP4=/path/to/clip.mp4 npm start
```

Or drop a file at `assets/sample.mp4` (gitignored). To rebuild the default local clip:

```bash
rm -f assets/sample.mp4 && bash scripts/fetch-sample.sh
```

## What you get

| Piece | Behavior |
|---|---|
| HTTPS | Serves `web/dist` + `/config.json` |
| WSS `/ws` | HELLO → CONFIG → looped MP4 as H.264 + PCM 48 kHz stereo |
| Sync | One ffmpeg demux (`-re -stream_loop -1`) → video + audio fifos |
| Input | Touch/mouse/scroll/key → server **burns** click marks + event log INTO the H.264 (true E2E). Also logged at `/debug/server-marks`. |
| Session | One active stream; a new HELLO preempts the old one (`/debug/session`) |
| Audio | Starts **muted** (autoplay policy). Unmute to hear the movie. |

Overlay pipeline: `ffmpeg` decodes the MP4 to rgb24 → Node composites the
overlay (`mock-desktop.overlayFrame`) → `ffmpeg` re-encodes to H.264
(`overlay-stream.mjs`). Uses `-preset ultrafast` + drop-frame backpressure to
hold source fps so A/V stay in sync (lipsync).

Idle / disconnected: black stage (no local mock wallpaper).

## Playwright

```bash
npx playwright install chromium   # once
npm run test:e2e
```

## Env

| Variable | Default | Meaning |
|---|---|---|
| `PORT` | `8443` | HTTPS port |
| `PAIRING_CODE` | `123456` | `/config.json` PIN |
| `DROPPIX_WEB_ROOT` | `../../web/dist` | Static files |
| `DROPPIX_MOCK_MP4` | `assets/sample.mp4` | Source clip |
| `DROPPIX_MOCK_MP4_URL` | *(unset)* | `fetch-sample.sh` downloads this instead of Tears of Steel |
