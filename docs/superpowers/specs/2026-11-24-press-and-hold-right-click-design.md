# Press-and-hold → right-click — design

**Date:** 2026-11-24
**Status:** Withdrawn (2026-11-24). After on-device testing the user chose to keep only the two-finger tap. What shipped from this work: the real-origin desktop bounds (`58ba5ec`) and the time-spaced right-click (`ClickSchedule`). The hold detector was built and then removed: libinput cancelled the touch correctly, but Plasma's desktop (`AppletsLayout`, `AfterPressAndHold`) still entered Edit Mode, because its timer ignores touch cancels.

## Intent

Right-clicking on the tablet should feel like it does on a phone or a Windows touchscreen:
hold a finger still and the context menu opens **exactly under that finger**. The two-finger
tap (2026-07-03) stays, but in practice it was hard to discover and landed between the
fingers. Until 2026-11-24 it also landed on the wrong monitor (fixed in `29d7e8f`).

## Goals

- Holding one finger still for 600 ms right-clicks at the finger's position.
- The hold never *also* produces a left-click when the finger lifts.
- Works for every client (Android, Linux desktop client, web) with no client changes.

## Non-goals

- On-tablet visual feedback (a filling ring). Possible later on Android.
- A setting for the hold time.
- Removing the two-finger tap.

## Anti-goals (a failure even if the hold works)

- Normal taps, drags, scrolls or pinches are delayed or changed.
- A hold that fires the menu and then left-clicks whatever is under the finger.

## Approaches considered

1. **Detect on the tablet (Android)**: exact timing and room for a feedback ring, but only
   the Android app gets it.
2. **Detect on the PC (chosen by the user)**: one implementation serves all three clients
   with no app update. Costs: needs a clock tick (a still finger sends no events), timing
   is subject to network jitter, and there's no on-tablet feedback.
3. Hold with no touch cancel. Rejected: lifting after the menu opens is a left-click.

## Design

**Detector:** a pure `HoldGesture` in `host/src/tap_gesture.{h,cpp}` next to `TwoFingerTap`,
unit-tested the same way. It is fed every contact update plus a periodic tick with
`now_ms`. It fires once per touch sequence when:
- exactly one contact is down, and no second finger has joined this sequence;
- that contact has moved less than `kHoldSlop` from where it went down
  (≈1.5% of the output, about 1000 of 65535);
- it has been down at least `kHoldMs = 600`.

The result carries the contact's current position (0..65535 on the droppix output). After
firing, the sequence is spent until every finger lifts. `TwoFingerTap` must not also report
a click for a spent sequence.

**Clock:** the stream loop already wakes at least every 8 ms whenever touch is on
(`frame_timeout`). It calls a new `InputInjector::tick(now_ms)` each pass, so the hold is
detected within one loop pass of 600 ms even though a still finger sends nothing.

**Cancelling the touch:** when the hold fires, before clicking:
- The touch device starts advertising `ABS_MT_TOOL_TYPE` (`MT_TOOL_FINGER`..`MT_TOOL_PALM`)
  and sends `MT_TOOL_FINGER` for each new contact.
- The held contact's slot is switched to `MT_TOOL_PALM` + `SYN_REPORT`. libinput (1.32 here)
  turns a touchscreen contact that becomes a palm into a touch **cancel**, so KWin and the
  app see a cancel instead of a release, and no click is synthesized.
- Later updates for that finger are dropped until it lifts. Its lift (tracking id −1) is
  still emitted so the slot is freed.

**The click:** a right press + release through the existing `droppix-rightclick` pointer, at
the finger's position, mapped with the same `scale_x`/`scale_y` as the client's
MouseButton message. That is the droppix output rect within the desktop bounds of the
enabled outputs.

**Getting the location right:**
- The hold uses the finger's own point, not a midpoint.
- `desktop_bounds()` assumes the desktop starts at 0,0. KWin's workspace starts at the
  top-left of the enabled outputs, which can be negative. Compute the bounds as min..max of
  the enabled outputs and map relative to that origin. Today's layout starts at 0,0, so this
  is a correctness fix for other layouts, not a change here.

## Risks

- **Touch cancel through the stack:** if KWin or the app ignores the libinput cancel, the
  lift still left-clicks. Verify on the device first. The fallback is to move the contact
  off the output before lifting.
- **Holds meant as a long left-press** (hold still, then drag) become right-clicks. Accepted.

## Testing

- **Unit** (`test_tap_gesture.cpp`):
  - a still hold fires after 600 ms at the finger's position, exactly once;
  - movement beyond the slop prevents it;
  - a second finger prevents it;
  - lifting resets it;
  - a two-finger tap still fires and a hold doesn't also fire it;
  - the min-origin bounds case.
- **On device (user):**
  - holding on the desktop and in a file manager opens the menu under the finger, with no
    extra left-click;
  - taps, drags and scrolls behave as before.

## Left to the builder

The exact slop value after a feel-test; how `tick()` is wired into the loop; MtSlots
bookkeeping for the cancelled slot.
