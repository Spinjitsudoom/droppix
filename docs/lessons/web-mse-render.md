# Web client: MSE `<video>` render path for low-end devices

**ID:** G-2026-08-06-web-mse-render · **Tags:** client, performance, gotcha · **Severity:** medium

## Context

On a budget phone (Redmi 9 / Helio G80) the web client played at 2–5 fps even at 720p.
The bottleneck was **client-side**, not the host: kbps (measured on received bytes) was
high while painted fps was low. The WebCodecs→Canvas 2D path is expensive on weak GPUs —
a per-frame `drawImage(VideoFrame)` copy, made far worse by `ctx.filter` (which forces a
slow raster path even at the identity `brightness(1) contrast(1)`), and WebCodecs may not
hardware-accelerate at all.

## What works

Add an alternative renderer that muxes H.264 into fragmented MP4 **client-side**
(`fmp4.ts`, host protocol unchanged) and feeds a `<video>` via Media Source Extensions,
so the browser's native (hardware) decoder + compositor do the work. Selectable via a
Renderer setting; default stays WebCodecs so nothing regresses for working clients.

## Gotchas that cost time

- **Input surface.** The `<canvas id="video">` is both the video surface *and* the pointer
  target. For MSE, keep the canvas on top as a **transparent input overlay** (z-index 1,
  `pointer-events` on) over the `<video>` (z-index 0). InputBinder is unchanged; its
  `contentBox()` letterbox math still maps coordinates as long as the canvas overlays the
  video element 1:1 and both use the same fit mode.
- **Opaque black fills hide the video.** The canvas CSS `background:#000` and the mock
  overlay's `clearCanvas()` (which filled black) both cover the `<video>`. Fix: clear the
  canvas to **transparent** (`clearRect`, not a black `fillRect`) and set
  `#app.render-mse #video { background: transparent }`. The CSS `#000` still shows black in
  canvas mode; MSE mode reveals the video.
- **Even dimensions.** H.264 requires even width/height; the muxer rounds fixed resolutions
  down to even, and Annex-B→AVCC must strip SPS/PPS/AUD from sample data (they live in avcC).
- **Gapless durations.** Hold one frame so each fMP4 segment's duration is `nextPTS−thisPTS`
  (exact), avoiding MSE stalls at buffer gaps. Costs ~1 frame of latency.
- **Live edge + memory.** Append per frame, trim buffered ranges older than ~2 s, and seek
  to near the buffered end when drift > ~0.6 s, or latency grows unbounded.
- **No tsc gate.** The web build is esbuild-only (imports use `.ts` extensions that plain
  `tsc` rejects), so type-check new code with `tsc --noEmit --allowImportingTsExtensions`
  manually — the build won't catch type errors for you.

## Where

`web/src/fmp4.ts` (muxer, unit-tested in `web/tests/fmp4.test.ts`),
`web/src/mse-decoder.ts` (`MseVideoPipeline`), `web/src/video-renderer.ts` (shared
interface), `web/src/main.ts` (`makeVideo()` picks the path), `web/public/{index.html,styles.css}`.
