# Auto-orientation: tablet drives the droppix monitor orientation

**Status:** Designed + implemented (2026-06-27). Revised after device testing: the
first cut used KWin output rotation with the app landscape-locked; the user wanted the
**tablet's own display to auto-rotate**, which conflicts with KWin rotation (Android's
auto-rotate already orients the picture, so a host rotation shows up as a skew). This
spec describes the chosen approach ("Model Z"): the host streams **portrait- or
landscape-shaped pixels**, and the Android app **unlocks its orientation** so Android
rotates the display naturally.

## Goal

Physically rotating the tablet reorients the extended monitor automatically: a real
reflowed portrait workspace when held portrait, landscape when held landscape, with no
manual step. Touch keeps working across orientations.

## Key insight

Only the host (KWin) can reflow a workspace to portrait. The cleanest way to get a
portrait workspace that the tablet displays upright/fullscreen — without any rotation
math or host/Android rotations fighting — is for the host to make the droppix output
**natively portrait-shaped** (swap W/H) and let Android's auto-rotate orient the
picture. No `kscreen-doctor` rotation is used.

The host only needs **portrait vs landscape** (2 states), not 4: the up/down flips
(0°↔180°, 90°↔270°) are handled visually by the tablet's Android auto-rotate.

## Data flow

1. App (Activity `screenOrientation=fullSensor`) auto-rotates with the device. An
   `OrientationEventListener` + `OrientationMapper` (dead-zone + settle) detect the
   physical orientation and send a 1-byte `ORIENTATION` message (code 0/1/2/3) on a
   settled change and once on connect.
2. Host records the latest code. While it stays in the current session's dimension
   class (both portrait or both landscape), nothing changes.
3. When the code crosses the portrait↔landscape boundary, the daemon ends the session
   and closes the client. The reconnect loop (`stream_main`) rebuilds the source at the
   swapped dimensions; the app's existing connect loop redials (~1s flicker, accepted).
4. The new session streams portrait- (HxW) or landscape- (WxH) shaped video; the app
   shows it at the CONFIG size; Android auto-rotate orients it upright, fullscreen.

## Components

### Wire protocol
- `ORIENTATION = 8` (app→host), 1-byte `u8 code` (0/1/2/3 ⇒ 0/90/180/270). HELLO
  version 2. (Unchanged from the first cut; still used — it's how the tablet tells the
  host which shape to stream.)

### Host
- `orientation.h`: `orientation_is_portrait(code)` ⇒ code 1 or 3 (pure, unit-tested).
  The earlier `orientation_degrees` + `apply_rotation` (kscreen rotation) are removed.
- `stream_main`: a file-scope `g_orientation` (code) seeded from `--orientation/90`.
  Each session computes dims = portrait ? (H,W) : (W,H), builds the evdi source at
  those dims, and passes `&g_orientation` to the daemon via `StreamConfig`.
- `StreamDaemon::run_until`: installs an orientation handler that writes the reported
  code to `*cfg_.live_orientation` and, if the code's class differs from this session's
  (`h > w`), sets a restart flag that ends the loop and `close_all()`s the client.
- Touch is unchanged: still binds the `droppix-touch` device to the droppix output via
  `outputName`. The output is now natively portrait/landscape, so touch coords map
  directly (no rotation).

### Android
- `AndroidManifest`: `screenOrientation` `landscape` → `fullSensor` (keeps
  `configChanges=orientation|screenSize|keyboardHidden`, so rotation doesn't recreate
  the Activity or drop the surface).
- `OrientationMapper` (pure, unit-tested), `TransportClient.sendOrientation`, and the
  `OrientationEventListener` wiring are unchanged. Sending on a settled portrait↔
  landscape change triggers the host restart; the existing reconnect loop handles the
  redial. The app displays the CONFIG-sized video as it already does.

### GUI
- The Orientation dropdown remains as the **initial** orientation (seeds `g_orientation`
  before the tablet reports). Labels now effectively mean landscape (0/180) vs portrait
  (90/270); cosmetic relabeling is deferred (YAGNI).

## Testing
- Host: `orientation_is_portrait` + protocol round-trip/wire-layout (72 host tests).
- Kotlin: `encodeOrientation` byte-match + `OrientationMapper` bucketing/settle.
- Android: `assembleDebug` + `testDebugUnitTest`.
- On-device: rotate tablet → display + workspace reorient (≈1s reconnect on the
  portrait↔landscape crossing); touch tracks. Calibrate `QUARTER_TO_CODE` if a
  direction is reversed.

## Out of scope / deferred
- Smooth (no-reconnect) resolution change mid-session (would need encoder/evdi re-init
  without dropping the client).
- Auto-rotate on/off toggle; GUI relabel of the orientation dropdown.
- Stylus/pressure (separate phase).
