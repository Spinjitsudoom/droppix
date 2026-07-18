> **Superseded for day-to-day use:** current protocol is HELLO v5 with types 1–15.
> See [`docs/WIRE.md`](../../WIRE.md) and `host/src/protocol.h`. This file remains the Phase-1a historical note.

# droppix Wire Protocol (Phase 1a)

Single TCP connection. Every message: `[u32 big-endian length][payload]`, where
`length` covers the payload, `payload[0]` is a 1-byte type, `payload[1..]` is the
body. All multi-byte integers are big-endian.

Types: Hello=1, Config=2, Video=3, Ping=4, Pong=5, Bye=6.

- HELLO (client→host): u32 version (current=1), u32 width, u32 height, u32 density.
- CONFIG (host→client): u32 width, u32 height, u32 fps, u32 extradata_len, extradata bytes.
- VIDEO (host→client): u64 pts_us, u8 keyframe, then the H.264 Annex-B access unit.
- PING/PONG: opaque body echoed back.
- BYE: no body.

IMPORTANT for the decoder: the host encodes H.264 with x264 repeat-headers, so
SPS/PPS travel IN-BAND ahead of every IDR. CONFIG.extradata is therefore usually
EMPTY — Android MediaCodec must be configured from the in-band IDR (the first
keyframe's SPS/PPS), not from CONFIG.extradata.
