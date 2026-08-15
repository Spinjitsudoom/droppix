# "Low fps" on a static screen is correct — and audio PCM is the baseline traffic

**ID:** G-2026-08-15-static-screen-low-fps · **Tags:** host, evdi, audio, performance, gotcha · **Severity:** medium

## The trap

A web client showed 1–4 fps and the stream felt broken. Days went into the client
(renderers, resolution, MSE), then the host (encoder, transport, bufferbloat pacing).
The pipeline was fine the whole time.

evdi capture is **damage-driven**: `grab()` returns a frame only when the compositor
repaints the virtual output. A static desktop legitimately produces almost no frames.
The client's fps counter reports *frames received*, so a still screen reads as "1–4 fps"
— indistinguishable, at a glance, from a broken stream.

**Measured on the same live session:**

| droppix screen | fps | throughput | median burst |
|---|---|---|---|
| static | 3.8 | 1.65 Mbps | 64.4 KB |
| animated (`glxgears` on that output) | **31.3** | 5.62 Mbps | 16.3 KB |
| static again | 2.9 | 1.52 Mbps | 64.4 KB |

## The second trap: that baseline is audio, not video

On the static screen the "3 fps of 64 KB frames" were not video at all. Audio is
**uncompressed** s16le 48 kHz stereo (`audio_streamer.cpp`): 192,000 B/s = **1.54 Mbps**,
emitted in ~340 ms chunks of ~64 KB. That matches the measured baseline exactly. Video
was ~0, correctly.

So with audio on, a droppix session never goes quiet: it always carries ~1.5 Mbps of PCM
regardless of screen activity. Worth knowing when budgeting a weak link — and an obvious
future optimisation (Opus).

## How to diagnose this without touching the (root-owned) streamer

`ss -tin` on the client connection answers "is it the network?" outright:

- `app_limited` present → **the sender isn't producing enough data**; the network and the
  peer are exonerated. Stop looking at the client.
- `delivery_rate`, `snd_wnd`, `minrtt`, `retrans` characterise the link (here: 176 Mbps,
  540 KB window, 0.9 ms, negligible loss — a healthy link).

Frame rate can be counted from outside the process by sampling `bytes_sent` at ~20 ms and
counting bursts (`scratchpad` `fps-from-net.py`) — useful when the streamer runs as root
under pkexec and `/proc/<pid>/syscall` and ptrace are unavailable.

**Always measure with motion on the droppix output.** `glxgears -geometry WxH+X+Y`
positioned inside that output's geometry (from `kscreen-doctor -o`) forces continuous
damage; XWayland honours the position hint even on a KDE Wayland session.

## Rule of thumb

Before investigating "low fps", confirm the screen is actually changing. Compare a static
sample against an animated one — if the animated number is healthy, there is no bug.

## Where

`host/src/capturer.cpp` (damage-driven `grab`), `host/src/evdi_frame_source.cpp`,
`host/src/audio_streamer.cpp` (PCM format), `host/src/stream_daemon.cpp` (capture loop).
