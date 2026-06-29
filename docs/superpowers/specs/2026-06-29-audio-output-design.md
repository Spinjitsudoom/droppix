# Audio Output to the Tablet — Design

**Goal:** Stream a dedicated audio channel from the PC to the droppix tablet, so an app's audio (e.g. a Wii U emulator's *gamepad* audio in Cemu) can play on the tablet while the PC speakers/TV keep their own audio.

**Architecture:** The host exposes a virtual PipeWire sink (`droppix-audio`); the user routes an app's output to it. The streamer captures that sink's monitor as raw PCM (via `pw-record` run in the user session), frames it as a new `AUDIO` wire message on the **existing** TLS connection, and the tablet plays it through `AudioTrack`. Audio is strictly best-effort and never blocks the display path.

**Tech stack:** C++ host streamer + Qt GUI, PipeWire (`pw-record`, `pactl`), the existing length-prefixed TLS wire protocol, Kotlin/Android `AudioTrack`.

## Global Constraints

- **Fixed audio format everywhere:** 48000 Hz, signed 16-bit little-endian (`s16le`), 2 channels (stereo, interleaved). Defined once as shared constants on both ends; no per-message format header. Changing the format is a protocol bump.
- **Best-effort:** a missing tool, missing sink, capture failure, or `AudioTrack` failure logs and continues **video-only**. Audio must never stall or break video/touch.
- **No new build dependencies:** capture uses the `pw-record` CLI (already present); raw PCM needs no codec. Do **not** link libpipewire or add libs.
- **PipeWire runs in the user session:** PipeWire is per-user. The GUI is itself a user process, so it runs `pactl` (sink management) directly. The streamer is root (pkexec), so it spawns `pw-record` through the existing run-as-user prefix (`user_cmd_prefix()`, the same mechanism used for kscreen and touch-bind).
- **Reuse the existing connection:** no new port/socket. New message `MsgType::Audio = 9` (next free id; `Orientation = 8` is the current max).
- **Single-threaded TLS:** all socket writes stay on the stream loop thread (audio is funnelled to it via a queue) so SSL_read/SSL_write never run concurrently across threads.

## Components

### Host

- **`host/gui/audio_sink.{h,cpp}` (new) — `DroppixAudioSink`.** Owns the virtual sink's lifecycle in the GUI (user) process.
  - `ensure()`: if a sink named `droppix-audio` doesn't already exist, create it:
    `pactl load-module module-null-sink sink_name=droppix-audio sink_properties=device.description="droppix-audio"` (run as the user). Store the returned module index.
  - `release()`: `pactl unload-module <index>` if this instance created it (don't unload a pre-existing one).
  - Idempotent: detect an existing `droppix-audio` (via `pactl list short sinks`) and adopt it without creating a duplicate.
  - Called on GUI launch (`ensure()`) and GUI shutdown (`release()`), so the sink is present for the whole GUI session and routable any time.

- **`host/src/audio_streamer.{h,cpp}` (new) — `AudioStreamer`.** Captures PCM off the sink's monitor.
  - `start()`: spawn, in the user session,
    `pw-record --raw --target=droppix-audio.monitor --format=s16 --rate=48000 --channels=2 --latency=20ms -`
    and read its stdout on a dedicated reader thread. Each read (bounded, ~≤8 KB) is pushed as a `std::vector<unsigned char>` onto a mutex-guarded queue.
  - `drain(out)`: move all queued chunks to the caller (called by the stream loop). Non-blocking.
  - `stop()`: terminate `pw-record`, join the reader thread.
  - On spawn failure / EOF: log once, mark inactive, leave the queue empty (video continues).

- **`host/src/protocol.{h,cpp}`** — add `Audio = 9`. `encode_audio(pcm)`/`decode_audio(body, pcm)` are pass-through (body *is* the interleaved s16le PCM), defined for symmetry and to anchor a shared test vector.

- **`host/src/transport_server.{h,cpp}`** — add `send_audio(const std::vector<unsigned char>& pcm)` = `send_all(encode_message(MsgType::Audio, pcm))`. Same single-threaded send path as video.

- **`host/src/stream_daemon.cpp`** — when `cfg_.audio`: construct `AudioStreamer`, `start()` it after the client connects. Each loop iteration, `audio.drain()` and `tx_.send_audio()` each chunk (on the loop thread). Enable the tight loop tick when `cfg_.touch || cfg_.audio` (already 8 ms for touch). `stop()` on session end.

- **`host/src/stream_main.cpp`** — parse `--audio` → `cfg_.audio = true`.

- **`host/gui/settings.h`** — `bool audio = false;` (persisted with the rest).
- **`host/gui/args_builder.cpp`** — append `--audio` when `settings.audio`.
- **`host/gui/main_window.{h,cpp}`** — a **"Stream audio to tablet"** checkbox (mirrors the Touch toggle): writes `settings.audio`. Construct/own a `DroppixAudioSink`; `ensure()` in the window ctor, `release()` in the dtor.

### Tablet

- **`android/app/src/main/java/com/droppix/app/audio/AudioPlayer.kt` (new).** Wraps `AudioTrack` (`STREAM_MUSIC`, 48000, `CHANNEL_OUT_STEREO`, `ENCODING_PCM_16BIT`, `MODE_STREAM`, buffer = a small multiple of `getMinBufferSize` for low latency). Owns a bounded PCM queue and a dedicated playback thread that drains the queue into `AudioTrack.write()` (blocking write provides backpressure there, not on the net thread). `submit(pcm)`, `start()`, `release()`. Init failure → logs, `submit` becomes a no-op.

- **`android/…/protocol/Protocol.kt`** — `MsgType.AUDIO` + pass-through decode (body is PCM). Shared `AUDIO_RATE=48000`, `AUDIO_CHANNELS=2` constants.

- **`android/…/net/TransportClient.kt`** — route `MsgType.AUDIO` → an `onAudio(pcm)` callback (added to `StreamListener`), which calls `audioPlayer.submit(pcm)`. The net thread only enqueues; it never blocks on playback.

- **`android/…/ui/StreamActivity.kt`** — create the `AudioPlayer` in `startStreaming`, wire `onAudio`, `release()` in `stopStreaming`.

## Data flow

```
Cemu (user routes gamepad audio) ─► [droppix-audio sink] ─► droppix-audio.monitor
  └─ pw-record --raw (user) ─► stdout (s16le/48k/stereo)
       └─ AudioStreamer reader thread ─► queue
            └─ stream loop: drain ─► tx.send_audio() ─► AUDIO msg ─► TLS (shared w/ video)
                 └─ tablet net thread ─► onAudio ─► AudioPlayer queue
                      └─ playback thread ─► AudioTrack.write()
```

Expected end-to-end latency ≈ `pw-record` (~20 ms) + loop tick (≤8 ms) + network + tablet buffer ≈ 50–80 ms — adequate for emulation; not frame-locked A/V sync.

## Error handling

Every audio failure is non-fatal and video-only is the fallback:
- `pw-record` missing or spawn fails → log once, no audio.
- `droppix-audio` sink missing (e.g. user removed it) → `pw-record` target fails → log, no audio.
- `AudioTrack` init/underrun on the tablet → log; underruns self-recover (it's a live stream, drop is acceptable).
- Connection drop → audio stops with video; both resume on reconnect.

## Testing

- **Protocol:** `AUDIO` encode/decode round-trip; one shared host(C++)↔Kotlin byte vector (same discipline as touch/orientation).
- **Host `AudioStreamer`:** feed bytes through a fake pipe/source (not live `pw-record`) and assert framing/queue behavior; assert clean shutdown.
- **Host `DroppixAudioSink`:** unit-test the "adopt existing vs create + unload only what we created" logic against a faked `pactl` runner.
- **Tablet:** test `MsgType.AUDIO` routing → `AudioPlayer.submit` with a fake player (JVM); `AudioTrack` itself is exercised manually.
- **Manual:** route Cemu's gamepad output to `droppix-audio`, connect the tablet, confirm gamepad audio plays on the tablet while TV audio stays on the PC.

## Out of scope (YAGNI for v1)

- Opus/compressed audio (raw PCM chosen; revisit if WiFi bandwidth demands it).
- Frame-accurate A/V sync (best-effort low latency only).
- Multiple/selectable sinks or a device picker (single fixed `droppix-audio` sink).
- Tablet→PC microphone / return audio.
- In-app volume beyond the tablet's normal media-volume control.
