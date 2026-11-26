# L-2026-11-26-live-audio-waited-for-a-frame: "Audio: on" did nothing over a still desktop

- **ID:** `L-2026-11-26-live-audio-waited-for-a-frame`
- **Tags:** `host`, `audio`, `silent-failure`, `medium`
- **Date:** 2026-11-26

## Symptom

Turning audio on from the tablet's menu did nothing until something moved on the PC screen (once
for 80 s). fps/quality changes were equally late.

## Root cause

`StreamParams` were applied in the frame path, after `src_->next()` returned a frame. evdi only
delivers frames on damage, so a still desktop never got there.

## Fix

The request is split (`take_audio_request` / `take_video_params`): audio is applied on every loop
pass, fps/bitrate with the next frame (an encoder reopen needs one to send, and nothing on screen
changes before it anyway). The frame wait also shortens once audio is on mid-session.

## Takeaway

Anything control-plane must not hang off the damage-driven frame path.
