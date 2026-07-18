# L-2026-07-18-mock-overlay-lipsync: A/V drift when re-encoding movie with burned-in overlay

- **ID:** `L-2026-07-18-mock-overlay-lipsync`
- **Tags:** `encoder`, `audio`, `performance`, `medium`, `lesson`
- **Date:** 2026-07-18
- **Related:** `L-2026-07-18-delta-drop-corruption`

## Symptom

Mock host burns a server-authored overlay (click marks + event log) into the movie by decoding the MP4 to raw frames, compositing in Node, and re-encoding to H.264. Audio (which bypasses the encoder) drifted slightly ahead of video - a small but real lipsync error that grew over time.

## Root cause

Two things in `tools/web-mock-host/src/overlay-stream.mjs` + `h264-desktop.mjs`:

1. `createRgbEncoder` set no x264 `-preset`, so it used `medium`. Node's single-threaded composite loop could not encode 1280x534@24 in realtime (~16-20fps), so video fell behind audio (progressive drift).
2. Backpressure paused the decode fifo when the encoder was busy. That built a frame backlog during warmup which later drained as a burst, pushing video ahead of audio.

Client side, the audio worklet prebuffered 120ms, adding a constant audio lag.

## Fix

- Encoder: `-preset ultrafast` (load-bearing for realtime 720p in Node). Confirmed steady ~24fps.
- Realtime backpressure: **drop** completed frames when the encoder is full instead of pausing the decoder (a fifo pause stalls audio too, then bursts). Keep draining the fifo so the decoder never blocks. See `encReady` in `overlay-stream.mjs`.
- Reuse a ring of frame buffers (no per-frame 2MB `Buffer.from`) to keep GC out of the realtime loop.
- Client: reduce `audio-worklet.ts` prebuffer 120ms → 60ms (localhost jitter is low).

## How to detect this in the future

- Measure steady-state (ignore ~2s warmup): emitted video fps must equal source fps and audio seconds must equal wall seconds. If video fps < source fps, expect lipsync drift.
- Anti-pattern: pausing a decode fifo for backpressure in a realtime A/V pipeline (creates bursts). Prefer dropping frames.
- Anti-pattern: libx264 without `-preset ultrafast/veryfast` on a realtime software-composited path.
