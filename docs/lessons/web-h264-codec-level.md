# G-2026-08-05-web-h264-codec-level: web client showed no video (hardcoded WebCodecs codec string)

- **ID:** `G-2026-08-05-web-h264-codec-level`
- **Tags:** `client`, `encoder`, `wrong-answer`, `silent-failure`, `high`
- **Date:** 2026-08-05
- **Related:** *(none)*

## Symptom

The web PWA client **connects fine but shows no video** (black canvas) against a
real `droppix_stream` host — while the **mock host** (`tools/web-mock-host`) and
the **Android** client both play video normally. No obvious error in the UI.

## Root cause

`web/src/decoder.ts` configured WebCodecs with a **hardcoded** codec string
`avc1.42E01F` — Constrained Baseline, **Level 3.1** (max ~1280×720). But droppix's
encoders set **no explicit profile/level**:

- `software_encoder.cpp` (x264, `preset ultrafast`) emits Constrained Baseline but
  the **level tracks resolution** — verified with ffmpeg: 1280×720 → Level 3.1,
  **1920×1080 → Level 4.0**.
- `nvenc_encoder.cpp` / `vaapi_encoder.cpp` set no `profile` → default to **High/Main**.

The web client sends `width/height = canvas.clientWidth * devicePixelRatio`, so on a
normal tablet it streams at ≥1080p → the stream is **Level 4.0+ (and often High
profile)**, which **exceeds** the `avc1.42E01F` the decoder declared. Chrome's
`VideoDecoder` refuses to decode a stream above its declared level/profile → frames
decode to nothing → black screen. The mock host works only because it forces
`-profile baseline -level 3.1`; Android works because MediaCodec is configured from
the stream's SPS, not a fixed string.

## Fix

Derive the codec string from the **actual stream's SPS** (in the first in-band
keyframe — the encoders repeat SPS/PPS per IDR, so `EncodedVideoChunk.data` carries
it). New pure helper `web/src/avc-codec.ts` `avcCodecString(nal)` walks the Annex-B
NAL units, finds the SPS (`nal_unit_type == 7`), and builds `avc1.` + hex of
`profile_idc`, the constraint-flags byte, and `level_idc`. `decoder.ts` configures
with that (fallback to `avc1.42E01F` only if parsing fails). Now the web client
decodes whatever profile/level the host sends, like Android.

## How to detect this in the future

- Web client black-screen while Android / mock host play → suspect a codec-string vs
  stream profile/level mismatch. Log/inspect `__droppixDebug().video` (`received` > 0,
  `painted` 0, and/or a `VideoDecoder`-ish `lastError`).
- Never hardcode a WebCodecs H.264 codec string; derive it from the SPS. Any encoder
  change that raises profile/level (or a higher client resolution) will otherwise
  silently break web playback while other clients are fine.
