# droppix wire protocol (current)

**Canonical implementation:** `host/src/protocol.{h,cpp}` and `android/app/src/main/java/com/droppix/app/protocol/Protocol.kt` (byte-identical; locked by shared test vectors).

**Protocol version:** `kProtocolVersion = 6` (HELLO body).

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
| 1 | Hello | client → host | Capabilities + identity (v6) |
| 2 | Config | host → client | Negotiated width/height/fps + optional extradata |
| 3 | Video | host → client | H.264 Annex-B AU + pts + keyframe flag |
| 4 | Ping | either | Latency / liveness |
| 5 | Pong | either | Echo |
| 6 | Bye | either | Deliberate disconnect (empty body) — see below |
| 7 | Input | client → host | Legacy single-pointer input |
| 8 | Orientation | client → host | Physical orientation |
| 9 | Audio | host → client | PCM audio chunks |
| 10 | Overlay | host → client | Stats / overlay control |
| 11 | Touch | client → host | Multi-touch contacts |
| 12 | Scroll | client → host | Wheel |
| 13 | MouseButton | client → host | Right/middle buttons |
| 14 | Key | client → host | Keyboard |
| 15 | Pen | client → host | Stylus pressure / eraser |
| 16 | KeyframeRequest | client → host | "my decoder lost sync — send an IDR now" (empty body) |
| 17 | StreamParams | client → host | live fps / bitrate / audio change on the running session |
| 20 | Pair | client → host | **WSS only** — 6 ASCII digits of the code shown on the PC |
| 21 | PairResult | host → client | **WSS only** — `[ ok u8 ][ tries_left u8 ]` |
| 22 | ClientSettings | either | **WSS only** — the web client's settings JSON, persisted by the host |

### Deliberate disconnect (type 6)

Empty body, client → host when the user presses **Disconnect** in the client's floating menu.

The host cannot otherwise tell a deliberate exit from a dropped link, and it treats those
oppositely: a drop is assumed temporary, so the streamer keeps the session and waits for the
tablet to dial back in. `Bye` closes the channel *and* ends the reconnect loop, so the
streamer exits and the host GUI removes the monitor.

This matters most over USB/AOA, where the reconnect loop re-runs the accessory handshake —
that relaunches the client app on the tablet and re-establishes the very session the user
just ended.

The client must ensure the message actually leaves before tearing the connection down; a
`Bye` still sitting in a send queue is indistinguishable from never having sent one.

Back-compatible: older hosts ignore the unknown type and fall back to drop handling.

### Keyframe recovery (type 16)

Encoders emit a keyframe every `gop_size = fps * 2` frames (~2 s). A client that loses
decoder sync discards frames until the next one, so without a way to ask it freezes for
up to the remaining GOP.

`KeyframeRequest` (empty body) makes the host force an IDR on the **next** encoded frame,
cutting that to about one round trip. It is one-shot: the flag is consumed per request, so
a client that keeps asking does not turn the stream all-intra.

Back-compatible in both directions: older hosts ignore the unknown type (recovery falls
back to the scheduled keyframe), and older clients simply never send it.

### Live stream params (type 17)

`fps`, `bitrate` and `audio` are negotiated in HELLO, so historically changing any of them
meant reconnecting. `StreamParams` applies them to the **running** session instead:

```
u32 fps
u32 bitrate_kbps
u8  audio_wanted
```

The host reopens its encoder with the new fps/bitrate and resends `Config`; the reopen
starts a fresh GOP, so the client resyncs on the next frame. Audio starts or stops its
capture without touching the video path. Requests are coalesced by overwrite — dragging a
quality slider emits a burst, and only the newest matters.

Encoders must therefore be safe to reopen mid-session (`Encoder::reset()`); before that
they allocated a fresh `AVCodecContext` over the old one and leaked it on every change.

**Resolution is deliberately not here.** It re-creates the host's virtual display, which
cannot happen inside a live session, so it still reconnects.

Back-compatible: older hosts ignore the unknown type (the client's setting simply does not
take effect until it reconnects), and older clients never send it.

## HELLO v6 body

```
u32 version
u32 width
u32 height
u32 density
u32 fps
u8  audio_wanted
u8  orientation_code
u32 bitrate_kbps
u16 wall_col              -- client-declared grid column (0-based); 0 = unset/default
u16 wall_row              -- client-declared grid row (0-based); 0 = unset/default
u16 name_len + name bytes
u16 id_len   + id bytes
```

Back-compatible with shorter v5/v4/v3/v2 bodies (missing fields default to 0 / empty):
a v5 body (no `wall_col`/`wall_row`) decodes with both fields as 0, and the name/id
strings still parse correctly (string offset shifts from 26 to 30 only for v6+ bodies).

## Video / headers

Encoders (NVENC, VAAPI, software x264) emit **in-band SPS/PPS ahead of every IDR**. `CONFIG.extradata` is typically empty. Decoders must configure from the first keyframe, not from CONFIG extradata.

## Security / pairing

WiFi (and other non-cable paths) wrap the stream in TLS with certificate pinning + a 6-digit pairing code. USB cable / AOA trust models differ; see the TLS PIN and AOA design specs.

## WebSocket binding (web PWA)

When `droppix_stream` is started with `--web --web-root <dir>` (TLS required), the same session port serves HTTPS static assets and accepts WebSocket upgrades at `/ws`. Message **bodies and MsgType IDs are unchanged**. Framing differs:

| Transport | Frame |
|---|---|
| TCP / AOA | `[ u32 be length ][ type u8 ][ body… ]` |
| WSS | one binary WebSocket frame = `[ type u8 ][ body… ]` (length = WS payload length) |

After TLS, the streamer sniffs the first bytes: HTTP → static / WSS path; otherwise → native Android/Qt length-prefixed client. `/config.json` returns `{ "pinRequired": true }` — the pairing code is **never** sent to the browser.

### Host-persisted web client settings (type 22)

The browser cannot keep its own settings reliably. `localStorage` is scoped to the exact
origin — scheme + host + **port**, and droppix's session port moves between runs — and
browsers decline to persist it for an origin whose certificate the user clicked through. So
settings disappeared between sessions even though the client stored them correctly.

The host holds the blob instead. Body is the client's settings object as UTF-8 JSON; the
host never interprets it, only stores and returns it (max 8 KiB).

```
host → client   immediately after a successful PairResult, ALWAYS sent (`{}` if none stored)
client → host   whenever the user changes a setting
```

The host→client frame is unconditional so the client can *wait* for it: `fps`, resolution and
`audio` travel in HELLO, so settings arriving afterwards would be a session late. The client
holds HELLO until the frame lands (with a short timeout as a guard, not a normal path).

**Gated by pairing**, exactly like the stream: the frame is only sent after the PIN is
accepted, and inbound blobs only reach the host over an already-paired connection. There is
no HTTP settings endpoint, so this adds no unauthenticated write surface.

The client merges the blob over its local copy but **never adopts `id`** — that is the
browser's identity in the host's approved-device store, and inheriting another device's would
inherit its approval (`mergeHostSettings`, tested).

Persisted host-side by the **GUI**, not the streamer: the streamer runs as root, so writing
the file there would leave it owned by root in the user's config dir. The streamer forwards
the blob on stdout and the GUI writes `web_client_settings.json`.

### Web pair handshake (host-verified PIN)

The page is host-served and loads/connects live, but the streamer **holds the video stream** until the browser proves it knows the code shown on the PC. On `/ws` open, before the streamer reads HELLO:

1. Client → host `Pair` (type 20) with the 6 ASCII digits the user typed.
2. Host compares (constant-time) against `derive_pairing_code(cert_der)` and replies `PairResult` (type 21) `[ ok ][ tries_left ]`.
3. On `ok=1` the client sends HELLO and streaming begins; on `ok=0` the client re-prompts. After **5** wrong attempts (`kWebPinMaxTries`) the host drops the connection.

The code is verified host-side only (`host/src/web_pin.h`, `verify_web_pin` in `web_frontend.cpp`); the browser never receives it. Native Android/Qt clients never send `Pair`/`PairResult` — those IDs are WSS-only.

The web PWA client speaks **HELLO v6** (`web/src/protocol.ts` `kProtocolVersion = 6`): its `encodeHello` writes the same `wall_col`/`wall_row` u16 pair, byte-identical to the C++/Kotlin codecs, so the toolbar "Wall" col/row inputs place the browser screen in the grid exactly like the native clients.
