# Phase 0 — evdi Spike Findings

**Date:** 2026-06-23
**Result:** ✅ SUCCESS — the evdi virtual-display + libevdi capture foundation works end-to-end on this machine.

This report records what the live hardware run proved, the exact observed
behaviour, and the concrete implications for the Phase 1 (streaming) plan.

## Environment confirmed

- Kernel `7.0.9-ogc3.2.fc44.x86_64` (Bazzite / Kinoite), KWin 6.6.5, Wayland.
- `libevdi` **1.14.16** and `evdi` kernel module **1.14.16** — versions match.
- GPU: `/dev/dri/card0` (libevdi attaches here as a DRM *slave*).

## What the run proved

Command (run on the host, as root):
```
sudo /home/Spinjitsudoomyt/droppix-build/droppix_spike 20
```

Observed output (abridged):
```
[libevdi] Process has master on /dev/dri/card0, err: Invalid argument
[libevdi] Opened /dev/dri/card0 as slave drm device
[libevdi] LibEvdi version (1.14.16) / Evdi version (1.14.16)
opened evdi node 0
Connected on evdi node 0. Waiting for KWin mode...
mode changed: 1920x1080 @ 32 bpp
Mode 1920x1080. ...
saved frame_0.png (1 dirty rects)
... (mix of saved frames and "timeout" frames) ...
Done. 9/20 frames saved.
[libevdi] Marking /dev/dri/card0 as unused
```

- ✅ A new **1920×1080 "droppix" monitor appeared in KDE Display settings** and KWin extended the desktop onto it.
- ✅ The EDID we generate is **valid** — KWin accepted it and set a 1920×1080 mode. (The Task-2 EDID bug fixes were necessary and sufficient.)
- ✅ Capture works: framebuffer pixels arrived with dirty rectangles, were
  converted BGRA→RGBA, and written as PNG.
- ✅ **Captured PNGs show the real monitor content with correct colours**
  (verified visually — `frame_10.png` showed a full-colour desktop wallpaper,
  skin tones rendered correctly, confirming the channel swap is right and the
  pixel format is XRGB8888 / BGRA little-endian).
- ✅ Clean teardown on Ctrl+C — the virtual monitor disappears.

## Key behavioural findings (these shape Phase 1)

1. **Capture is damage-driven, not fixed-FPS.** `grab()` returns a frame only
   when evdi reports new damage; otherwise it times out. The "timeout" lines in
   the run are windows where nothing on the virtual monitor changed — correct
   and *desirable* (no wasted work when the screen is static).
   - *Phase 1:* drive the encode/send loop off evdi's `update_ready` event (block
     on `evdi_get_event_ready()`'s fd), not a timer. Encode+send only on damage.
     Send a periodic keyframe/heartbeat so a late-joining or lossy decoder can
     resync, and so a static screen still recovers after packet loss.

2. **Dirty rectangles arrive merged — typically 1 rect/frame** (evdi's kernel
   painter merges damage once it reaches MAX_DIRTS=16). The `[16]` cap is
   correct and was confirmed never exceeded.
   - *Phase 1:* the merged rect can drive partial/region encoding later, but the
     simplest first cut is full-frame H.264 each damage event. Keep `Frame::rects`
     in the contract for a future partial-encode optimisation.

3. **Pixel format is 32bpp BGRA (XRGB8888), stride = width×4.** No `bpp != 32`
   warning fired, so the assumption holds on this hardware.
   - *Phase 1:* the VAAPI H.264 encoder wants **NV12** (YUV 4:2:0). The Encoder
     must convert BGRA→NV12 — prefer a GPU/zero-copy path (VAAPI/EGL import of
     the evdi buffer) or `libswscale` as a fallback. This is the main new piece
     of work in Phase 1's Encoder.

4. **The spike's 200 ms inter-grab sleep is artificial** (it was only to sample
   at ~5 FPS for the spike). Real latency was not measured.
   - *Phase 1:* remove the sleep; measure true capture→encode→decode→display
     latency with the PING/PONG + on-screen overlay from the design.

5. **`evdi_add_device()` requires root** (it writes sysfs). The spike ran under
   `sudo` successfully; evdi node 0 was used.
   - *Phase 1/5:* for unprivileged use, ship a udev rule granting the user access
     to the evdi add interface and `/dev/evdi*` (deferred to Phase 5 polish). For
     Phase 1 development, running the daemon with elevated privileges is fine.

6. **The "Process has master … err: Invalid argument / Opened as slave drm
   device" log is benign** — libevdi simply attaches to the existing DRM master
   (KWin). Not an error; no action needed.

## Carry-over notes from review (non-blocking)

- `grab()` currently deep-copies the whole framebuffer each call — fine for the
  spike, but the Phase 1 Encoder interface should not bake in the full-copy
  assumption (use `Frame` + rects).
- `Capturer` does not re-register the buffer on a mid-session mode change
  (documented limitation). Phase 1 should handle resolution changes (re-allocate
  + re-register + send a new `CONFIG`/keyframe).
- `buffer_id_` is hardcoded to 1 (single buffer). Revisit if Phase 1 adds
  double-buffering for the encode pipeline.

## Verdict

Phase 0's riskiest assumption — that evdi gives us a real extended monitor we can
capture in userspace with correct pixels on this exact kernel/KWin — is
**confirmed true**. The Phase 1 streaming plan (VAAPI H.264 encode → adb/WiFi
transport → Android MediaCodec) can proceed on this foundation, with the BGRA→NV12
conversion and damage-driven encode loop as the first concrete tasks.
