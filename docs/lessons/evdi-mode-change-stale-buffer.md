# L-2026-11-24-evdi-mode-change-stale-buffer: a mid-session mode change paired the new size with the old buffer

- **ID:** `L-2026-11-24-evdi-mode-change-stale-buffer`
- **Tags:** `host`, `evdi`, `encoder`, `wrong-answer`, `high`, `lesson`
- **Date:** 2026-11-24

## Symptom

(Found in review.) Changing the droppix screen's resolution while it streamed could garble
the picture or crash the streamer.

## Root cause

`Capturer::on_mode_changed` updated `width_` / `height_` / `stride_`, but the evdi buffer
stayed registered at the old size (a "Phase 0 limitation" note). `grab()` then labelled
the old-size pixels with the new size. For a larger mode, the encoder read past the end
of the buffer. The encoder, the client's decoder and the touch mapping also all still
used the session's original size.

## Fix

- `Capturer::grab` re-registers the buffer at the new size when a mode change arrives,
  and returns no frame for that round.
- The stream loop checks `frame_fits_session(f, w, h)`: same size as the session, and a
  pixel buffer of at least `stride*height`. On mismatch it restarts the session through
  the orientation-restart path. The rebuilt source reports the new mode and the client
  reconnects.

## How to detect this in the future

Log line `mode: screen is now WxH (session wxh); restarting stream`. Never let a frame's
stated size and its buffer disagree. Tests: `FrameFitsSession.*`.
