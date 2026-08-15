# spacedesk VIEWER protocol — reverse-engineering notes

**Status:** Partial — one direction shipped. Discovery, framing, the session-open
sequence and **video** are decoded and implemented: droppix serves a real spacedesk
viewer (`host/src/spacedesk_server.*`). Input, audio and orientation are still unknown,
and they gate full two-way compatibility.

Goal: let the proprietary spacedesk VIEWER app connect to droppix as if droppix were a
spacedesk server. Interoperability work — we control the server end, the user owns the
client. Observed against spacedesk VIEWER on Android (Redmi 9), 2026-08-15.

## Transport

spacedesk's own manual documents **TCP + UDP port 28252**
([firewall docs](https://manual.spacedesk.net/FirewallSettingsonPrimaryMachine.html)).
Above 1024, so both can be bound without root — no packet capture needed to observe the
client.

## Discovery — SOLVED

The viewer broadcasts, roughly 33×/second while its connect screen is open:

```
UDP <client>:<ephemeral> -> 255.255.255.255:28252     21 bytes
53 50 41 43 45 44 45 53 4b 2d 4e 45 54 2d 43 4c 49 45 4e 54 00
"SPACEDESK-NET-CLIENT\0"
```

The **real** reply is a 308-byte struct, since verified byte-identical against a live
spacedesk server:

| offset | field |
|---|---|
| `@0` | machine name, UTF-16LE, in a 260-byte fixed buffer |
| `@260` | TCP port to connect to (28252) |
| `@264` / `@272` / `@276` / `@280` | 8 / 4 / 8 / 43 — version+capability constants |

(A bare `SPACEDESK-NET-SERVER` string — no trailing NUL — is *enough to coax a
connection*, and was the first thing found, but it leaves the viewer on a degraded path.
Send the real struct.)

## Message framing — SOLVED

Every message is a **128-byte fixed header**, optionally followed by a payload:

| offset | type | meaning |
|---|---|---|
| `@0` | u32 LE | message type |
| `@4` | u32 LE | payload length (0 for header-only messages) |

Confirmed against all three captured messages: hello = `128 + 334 = 462` bytes with
`@4 = 334`; keepalives = 128 bytes with `@4 = 0`.

Observed client message types: **0** = hello, **12** = keepalive/waiting (sent repeatedly
while the client waits for a server that never answers).

## Client hello (type 0) — DECODED

Header fields (u32 LE) from a real capture:

```
@0  = 0        type (hello)          @32 = 60
@4  = 334      payload length        @36 = 65541   (u16 pair: 5, 1)
@8  = 4                              @44 = 262204  (u16 pair: 60, 4)
@12 = 8                              @48 = 1
@20 = 3                              @52 = 2340    <- viewer screen WIDTH
@24 = 3                              @88 = 1080    <- viewer screen HEIGHT
@28 = 2                              @124 = 1
```

Payload is UTF-16LE: `{9f09a77b-5128-4240-acb7-c95efab243ff}\0M2004J19C` — a stable device
GUID followed by the model string (`M2004J19C` is the Redmi 9). 2340x1080 matches that
device's panel, confirming the width/height reading.

## Implemented in droppix

`host/src/spacedesk_{protocol.h,server.{h,cpp}}` + `jpeg_stripe_encoder.{h,cpp}`: droppix
answers discovery and streams the display to a real spacedesk viewer as JPEG stripes,
enabled by default (`--no-spacedesk` / `DROPPIX_SPACEDESK=0` to disable). The session-open
sequence matters: the viewer stays connected with its display OFF unless it receives
ack + two type-3 display messages + two heartbeats before any frame.

## What is NOT known

- **Input** (touch/mouse/keyboard, client -> server) — never captured. It only appears
  when a real viewer drives a real server, so watching a server send cannot reveal it.
- **Audio**, either direction — no audio-bearing message has ever been observed.
- **Orientation** changes.
- Any version or licence gating.

`tools/spacedesk-re/capture_all.py` relays a genuine session and flags every message type
outside the known set, prompting through touch / drag / pinch / rotate / audio / keyboard
in turn. One run with the phone driving a real server should decode all of the above —
that is the gate on full two-way compatibility.

## Video — SOLVED

Server message **type 2** carries a plain **baseline JFIF image** — not a proprietary
codec, which is what made a droppix implementation possible at all. The display is split
into **4 horizontal stripes** (2340x1080 -> 4 x 2340x270), one JPEG each:

| offset | field |
|---|---|
| `@8` / `@12` | full display width / height |
| `@16` | stride (width * 4, i.e. 32bpp source) |
| `@28` / `@36` | stripe y start / y end |
| `@32` | stripe x end (= full width) |
| `@68` | payload length (repeated from `@4`) |

Session open, in order — the viewer sits connected with its display **OFF** until it has
all of these: `type 4` ack, `type 3` display `{@8=0,@12=1}`, `type 3` display
`{@8=1,@12=1}`, then two `type 1` heartbeats. A `type 1` heartbeat `{@8=15,@12=1}` follows
roughly every 10s.

## How this was decoded (the method that worked)

Guessing cost one phone tap per experiment: the viewer never reconnects on its own (zero
connections in 90s of answered discovery). The unlock was to stop guessing — run the real
spacedesk server in a Windows VM and **replay the viewer's captured hello at it**
(`tools/spacedesk-re/replay.py`), which yields the server side directly, unlimited
iterations, no phone in the loop. Input and audio cannot be learned this way, since they
flow the other direction; those need `capture_all.py` with a viewer driving a real
server.

## Tools

`scratchpad/spacedesk/`: `probe.py` (passive observer), `responder.py` (discovery-response
candidate search), `handshake.py` (capture + structural analysis),
`find_server_reply.py` (server-reply candidate enumeration).

## Note

spacedesk's EULA very likely prohibits reverse engineering. Interoperability exceptions
exist (EU Software Directive Art. 6, US DMCA §1201(f)), but that is worth checking before
any of this ships in a public repo.
