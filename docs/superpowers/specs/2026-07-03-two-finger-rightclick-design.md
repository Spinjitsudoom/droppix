# Two-finger tap → right-click — design

**Date:** 2026-07-03
**Status:** Shipped on master.

## Goal

A **two-finger tap** on the tablet opens the right-click context menu on the droppix
monitor. A touchscreen can't emit a button, so the host detects the gesture and synthesizes
a real right-click at the tap location.

## Components

### 1. Pure gesture detector — `host/src/tap_gesture.{h,cpp}` (unit-tested)

```cpp
class TwoFingerTap {
 public:
  struct Result { bool rightClick; uint16_t x; uint16_t y; };   // x,y = tap midpoint (0..65535)
  Result update(const std::vector<TouchContact>& contacts, int64_t now_ms);
};
```

Fed each touch update with a monotonic time. It tracks the current touch sequence and, when
all fingers lift, reports a right-click iff:
- the sequence peaked at exactly **2** simultaneous contacts (never 3+),
- neither finger moved more than `MOVE_THRESH` (2500 of 65535 ≈ 4%),
- the whole gesture lasted ≤ `TAP_MS` (400 ms),

with `x,y` = the last 2-contact midpoint. Single-finger taps (peak 1), scrolls (moved),
long holds (too slow), and 3+ fingers yield no click. Thresholds are tunable constants.

### 2. Injector — synthesize the right-click — `InputInjector`

- A **second uinput device** ("droppix-rightclick"): an absolute pointer with `BTN_LEFT`/
  `BTN_RIGHT`, `ABS_X`/`ABS_Y` ranged to the desktop size. Created by `set_geometry()` (which
  is when the desktop size is known).
- `set_geometry(out_x, out_y, out_w, out_h, desktop_w, desktop_h)` — records the droppix
  output rect within the desktop and (re)creates the right-click pointer at the desktop range.
- `inject(contacts)` runs the touch path as today AND feeds `TwoFingerTap`; on a detected tap
  it maps the midpoint (0..65535 on the monitor) to desktop pixels
  `(out_x + x*out_w/65535, out_y + y*out_h/65535)` and emits move + `BTN_RIGHT` press/release
  on the pointer device.
- The two tapping fingers still pass through as normal touch; a no-movement two-finger tap is
  a desktop no-op (2-finger gestures need movement), so it doesn't conflict.

### 3. Wiring — `stream_daemon`

After the droppix output is identified (already done for touch binding), call
`injector.set_geometry(droppix.geom.{x,y,w,h}, desktop_w, desktop_h)` so the right-click
pointer exists and maps correctly. Desktop bounds come from `--desktop`/kscreen (as today);
if unknown, the right-click pointer is skipped (touch still works).

## Testing

- **Unit** (`test_tap_gesture.cpp`): two-finger tap → click at midpoint; single-finger → none;
  moved (scroll) → none; too slow → none; 3 fingers → none; 1→2→1→0 sequence → click.
- **On-device (needs tuning):** two-finger tap opens the context menu at the right spot;
  scroll/pinch/single-touch unaffected. Timing/movement thresholds and the pointer→output
  mapping may need a pass or two on the Nexus.

## Risks

- How KWin/libinput maps a synthetic absolute pointer to an output isn't certain from here;
  the desktop-pixel mapping via the output rect is the first attempt. If it lands on the wrong
  monitor, the fallback is to bind the pointer to the output (KWin `outputName`) and inject raw
  normalized coords.

## Out of scope

- Configurable gesture (always two-finger tap). Per-session uniqueness of the right-click
  device name (folds into the multi-monitor project's `--touch-name` scheme later).
