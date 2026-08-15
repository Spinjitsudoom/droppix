# spacedesk VIEWER protocol — reverse-engineering notes

**Status:** Partial. Discovery and message framing decoded and reproduced; the server's
reply and the entire video path are unknown. **No implementation in droppix yet.**

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

The reply that makes the viewer list a server and open TCP :28252:

```
"SPACEDESK-NET-SERVER"        <- 20 bytes, NO trailing NUL
```

The NUL matters: `SPACEDESK-NET-SERVER\0` produced **zero** connections across ~88,000
replies; the same string without it produced connections. Reply to the datagram's source
address (broadcasting it as well is harmless).

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

- **The server's reply.** Untested; 18 candidates were prepared (enumerating the type
  field with the server's geometry in the client's own header shape) but could not be run
  — see the iteration cost below.
- **Display/format negotiation.** Unknown whether a raw/uncompressed pixel format can be
  negotiated. This decides whether a droppix implementation is feasible at all: spacedesk
  compresses with its own scheme, and synthesising a proprietary codec's bitstream
  blind — with no feedback beyond "did the client render something" — is close to
  infeasible. A negotiable raw RGB mode would be the viable path.
- **Input** (touch/mouse/keyboard, client -> server) — never captured. It only appears
  when a real viewer drives a real server, so watching a server send cannot reveal it.
- **Audio**, either direction — no audio-bearing message has ever been observed.
- **Orientation** changes.
- Any version or licence gating.

`tools/spacedesk-re/capture_all.py` relays a genuine session and flags every message type
outside the known set, prompting through touch / drag / pinch / rotate / audio / keyboard
in turn. One run with the phone driving a real server should decode all of the above —
that is the gate on full two-way compatibility.

## Iteration cost (the practical blocker)

The viewer **does not retry on its own**: with discovery answered continuously for 90 s
it opened **zero** connections. Connections only appeared when the user tapped the server
entry. So every protocol guess costs one manual tap on the phone, and the search space
(server reply shape, then format negotiation, then framing) is far larger than a
tap-per-experiment loop can cover comfortably.

Any serious continuation should first look for a cheaper feedback loop — e.g. running the
real spacedesk Windows server in a VM and capturing a genuine session, which yields the
server side directly instead of guessing at it.

## Tools

`scratchpad/spacedesk/`: `probe.py` (passive observer), `responder.py` (discovery-response
candidate search), `handshake.py` (capture + structural analysis),
`find_server_reply.py` (server-reply candidate enumeration).

## Note

spacedesk's EULA very likely prohibits reverse engineering. Interoperability exceptions
exist (EU Software Directive Art. 6, US DMCA §1201(f)), but that is worth checking before
any of this ships in a public repo.
