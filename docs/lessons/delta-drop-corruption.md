# L-2026-07-18-delta-drop-corruption: shaky/corrupted video from dropping delta frames

- **ID:** `L-2026-07-18-delta-drop-corruption`
- **Tags:** `client`, `encoder`, `wrong-answer`, `performance`, `high`, `lesson`
- **Date:** 2026-07-18
- **Related:** `L-2026-07-18-ghost-ws-stream`

## Symptom

Web client playback was "noisy, incomplete, shaky": smeared macroblocks and jumping motion. Audio crackled and popped.

## Root cause

- `web/src/decoder.ts` silently skipped frames when `decodeQueueSize > 10`. Dropping an arbitrary H.264 **delta** frame corrupts every following frame until the next IDR, so each backlog blip produced a burst of visual garbage.
- `web/src/audio.ts` (AudioBuffer fallback) scheduled one `AudioBufferSourceNode` per 20 ms network packet (~50 sources/s); any timing jitter produced boundary clicks. `unlock()` also played an 880 Hz test chirp on every connect.
- Mock encoder used `-level 3.0` for 720p, which is out of spec (level 3.0 tops out around 720x480@30); x264 bumps it silently while the decoder was configured for 3.0.

## Fix

- `decoder.ts`: on backlog, set `dropUntilKey` and discard the rest of the GOP, resyncing on the next keyframe; use server PTS for chunk timestamps; codec string `avc1.42E01F` (baseline 3.1).
- `audio.ts`: AudioWorklet first (SW unregistered on :8443 so addModule works), fallback aggregates PCM into ~200 ms scheduled buffers; chirp removed.
- `audio-worklet.ts`: 120 ms prebuffer before starting and after every underrun.
- `mp4-stream.mjs`: `-level 3.1 -crf 20 -maxrate 4500k`, GOP = 1s.

## How to detect this in the future

- Anti-pattern: any `return` that skips a **delta** chunk without setting a resync-on-keyframe flag.
- Visual signature: smearing that "heals" at each keyframe interval.
- E2E check: canvas center pixels should change ~fps times per second while streaming (paint-rate probe in the mock e2e).
