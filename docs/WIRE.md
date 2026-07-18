# droppix wire protocol (current)

**Canonical implementation:** `host/src/protocol.{h,cpp}` and `android/app/src/main/java/com/droppix/app/protocol/Protocol.kt` (byte-identical; locked by shared test vectors).

**Protocol version:** `kProtocolVersion = 5` (HELLO body).

Historical Phase-1a note: `superpowers/specs/2026-06-23-droppix-wire-protocol.md` (types 1–6 only). Prefer this file + `protocol.h` for anything newer.

## Framing

Single TCP (or AOA byte-channel) connection. Every message:

```
[ u32 big-endian length ][ payload ]
```

`length` covers the payload. `payload[0]` is the type byte; `payload[1..]` is the body. Multi-byte integers are big-endian.

## Message types

| Value | Name | Direction | Role |
|---|---|---|---|
| 1 | Hello | client → host | Capabilities + identity (v5) |
| 2 | Config | host → client | Negotiated width/height/fps + optional extradata |
| 3 | Video | host → client | H.264 Annex-B AU + pts + keyframe flag |
| 4 | Ping | either | Latency / liveness |
| 5 | Pong | either | Echo |
| 6 | Bye | either | Clean shutdown |
| 7 | Input | client → host | Legacy single-pointer input |
| 8 | Orientation | client → host | Physical orientation |
| 9 | Audio | host → client | PCM audio chunks |
| 10 | Overlay | host → client | Stats / overlay control |
| 11 | Touch | client → host | Multi-touch contacts |
| 12 | Scroll | client → host | Wheel |
| 13 | MouseButton | client → host | Right/middle buttons |
| 14 | Key | client → host | Keyboard |
| 15 | Pen | client → host | Stylus pressure / eraser |

## HELLO v5 body

```
u32 version
u32 width
u32 height
u32 density
u32 fps
u8  audio_wanted
u8  orientation_code
u32 bitrate_kbps
u16 name_len + name bytes
u16 id_len   + id bytes
```

Back-compatible with shorter v4/v3/v2 bodies (missing fields default to 0 / empty).

## Video / headers

Encoders (NVENC, VAAPI, software x264) emit **in-band SPS/PPS ahead of every IDR**. `CONFIG.extradata` is typically empty. Decoders must configure from the first keyframe, not from CONFIG extradata.

## Security / pairing

WiFi (and other non-cable paths) wrap the stream in TLS with certificate pinning + a 6-digit pairing code. USB cable / AOA trust models differ; see the TLS PIN and AOA design specs.
