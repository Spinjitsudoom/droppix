# L-2026-11-26-usb-cant-see-a-close: over USB neither side can see the other close

- **ID:** `L-2026-11-26-usb-cant-see-a-close`
- **Tags:** `host`, `android`, `transport`, `protocol`, `silent-failure`, `high`
- **Date:** 2026-11-26

## Symptom

On the Nexus 10 over USB (AOA), each of these hung or silently did nothing:
- choosing **Portrait** mid-stream: the PC rebuilt its screen and looped "no HELLO" forever; the
  tablet never reconnected;
- choosing a new **resolution**: the app said "Reconnecting…", the PC kept the old size;
- after the app gave up a session, the next open failed with EBUSY (`/dev/usb_accessory` is
  single-open, and a stale reader thread still held it).

## Root cause

An AOA link stays up whatever either end does. When the PC ended a session, the app's reader just
saw no data; when the app closed and reopened the accessory, the PC's libusb reads just timed out.
So the app sat on a dead session, and the PC kept streaming into a session the app had left,
discarding the new HELLO (it only looked for one before a session).

## Fix

- Host says `Bye` on every session it ends (`TransportServer::end_session`).
- Host pings a quiet link every second (also while waiting for HELLO), which wakes a stale reader;
  `PeerLiveness` ends a session after 5 s of silence from a client that pings.
- A HELLO mid-session means the client started over: end without `Bye`, keep the channel, serve
  the stashed HELLO (`client_restarted()`, stream_main reuses the channel).
- The app reopens if no `Config` arrives 8 s after its HELLO (a reader opened mid-message is
  mis-framed and would otherwise wait forever).

- App side: the AOA loop re-fetches the current accessory on every attempt and keeps trying for
  2 min with the cable attached (`AoaRetry`; it gave up after 4 s), and the connect screen
  re-adopts an attached accessory after a session (`aoaAdopted` used to stay set forever).

See WIRE.md: Bye, Liveness, A HELLO mid-session.

## Takeaway

On a link that cannot signal a close, every "the session is over" and "I am starting over" must be
an explicit message, and both sides need a watchdog.
