# Auto-orientation: tablet drives the droppix monitor rotation

**Status:** Designed (approved 2026-06-27). Supersedes the manual-only orientation
control added in `57f476a` by making rotation follow the device automatically; the
manual host control is retained as the default/initial value.

## Goal

When the user physically rotates the Android tablet, the droppix extended monitor
should automatically reorient to a real, reflowed workspace (true portrait/landscape),
with no manual GUI step. Touch must keep working across rotations.

## Key insight

The host owns the desktop layout; the app only receives a video of whatever the
droppix output currently is. So the app cannot turn a landscape desktop into a
portrait *workspace* on its own — only KWin can. The clean division of labour:

- The **app detects** physical orientation (Android sensor) and **reports** it.
- The **host rotates** the droppix KWin output to match.

Everything on the wire stays in one **un-rotated "landscape" reference frame** (the
1920×1080 video buffer and the touch coordinates). KWin applies the rotation once at
the output. The user physically turning the tablet provides the visual match, so:

- The app never rotates pixels and stays **landscape-locked** (the `SurfaceView`
  always matches the 1920×1080 stream).
- A rotated 1080×1920 desktop exactly fills the 1920×1080 scanout buffer (no scaling,
  no letterbox in the buffer); held in that orientation it reads upright, near
  full-screen.
- Touch coordinates remain landscape-space; the touch device is bound to the droppix
  output (existing `outputName` binding), and KWin un-rotates input for that output —
  so touch composes through rotations with **no app or protocol change to touch**.

## Data flow

1. App's `OrientationEventListener` reads the sensor angle (works even while the
   Activity is orientation-locked).
2. The angle is bucketed into one of four orientations with a dead-zone + debounce.
3. On a *settled* change (and once on connect), the app sends an `ORIENTATION`
   message to the host.
4. The host's transport dispatches it to an orientation handler, which calls
   `apply_rotation(droppix_output_name, degrees)` live, skipping redundant calls.
5. KWin rotates the output; the 1920×1080 scanout is unchanged in size (capture and
   encoder untouched), now containing the rotated desktop.

## Components

### Wire protocol (`host/src/protocol.{h,cpp}` + Kotlin mirror)

- New message type `ORIENTATION = 8` (app→host).
- Body: 1 byte, `u8 code` where `0→0°, 1→90°, 2→180°, 3→270°`.
- Add `encode_orientation(uint8_t code)` / `decode_orientation(body, code)` on the
  host and `Protocol.encodeOrientation(code)` + `MsgType.ORIENTATION(8)` in Kotlin.
- Byte-identical both ends, asserted by a host test and a Kotlin test. The framed
  message is `[u32 BE len][u8 type][body]`; `len` covers the type byte, so an
  ORIENTATION frame is `[0,0,0,2, 8, code]` (len = 2 = type + 1-byte body),
  mirroring how `INPUT` was verified.
- Bump the HELLO protocol version (wire-format change). Old apps simply never send
  the message; the host falls back to the `--orientation` default.

### Android (`android/…`)

- Lock the streaming Activity to landscape (`android:screenOrientation="landscape"`
  / `userLandscape`) so the `SurfaceView` always matches the 1920×1080 stream. No
  pixel rotation is performed by the app.
- An `OrientationEventListener` maps the sensor angle (0–359°) to one of four
  orientation buckets. Bucketing uses a **dead-zone** around the diagonals and a
  **~300 ms settle/debounce** so mid-tilt jitter never reflows windows. Pure
  bucketing logic (`angle → code`, with hysteresis state) is isolated so it can be
  unit-tested without a device.
- On a stable bucket change, and once immediately after connect, call
  `TransportClient.sendOrientation(code)` (thread-safe, like `sendInput`).
- The physical-orientation → host-rotation-code mapping has a documented default and
  is trivially adjustable (the 90°/270° direction is a calibration constant).

### Host (`host/src/transport_server.*`, `stream_daemon.cpp`)

- `TransportServer`: add `set_orientation_handler(std::function<void(uint8_t)>)`;
  `poll_control()` dispatches `MsgType::ORIENTATION` via `decode_orientation` to it
  (mirrors the existing input handler).
- `StreamDaemon::run_until`: it already identifies the droppix output name for
  touch/orientation. Capture that name and install an orientation handler that maps
  `code → degrees` and calls the existing `apply_rotation(name, degrees)` live,
  tracking the last applied value to skip redundant `kscreen-doctor` invocations.
- The existing `--orientation` flag remains the **initial/default** rotation applied
  at session start (before the app's first report); live `ORIENTATION` messages
  override it. No change needed to `apply_rotation` itself.

### GUI (`host/gui/…`)

- Unchanged in behavior: the Orientation dropdown stays as the **default/initial**
  orientation (already implemented + persisted). A future "lock rotation / disable
  auto" toggle is out of scope (YAGNI).

## Testing

- Host: `encode/decode_orientation` round-trip + exact framed-byte layout test.
- Kotlin: `encodeOrientation` matches the host wire bytes.
- Host: `code → degrees` mapping is a pure function with a unit test.
- Android: orientation-bucketing/hysteresis logic unit-tested (angle sequences →
  expected emitted codes), no device required.
- Manual on-device verification (the two known risks):
  1. The physical→rotation mapping direction (90° vs 270°); flip a constant if off.
  2. Touch composes correctly in portrait (KWin un-rotation of the bound device).

## Out of scope / deferred

- Host mode-swap (true portrait scanout) instead of KWin rotation — not needed; the
  rotate-output approach already yields a near-full-screen upright portrait.
- An auto-rotate on/off toggle or per-app rotation lock.
- Stylus/pressure (separate, later phase).
