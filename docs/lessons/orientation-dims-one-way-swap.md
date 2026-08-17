# L-2026-08-17-orientation-dims-one-way-swap: the shape fix only worked in one direction

- **ID:** `L-2026-08-17-orientation-dims-one-way-swap`
- **Tags:** `host`, `android`, `evdi`, `wrong-answer`, `regression`, `high`
- **Date:** 2026-08-17

## Symptom

Streaming to a phone held **landscape** over USB (AOA), the picture was a narrow vertical
strip in the middle of the screen with wide black bars either side — the host was sending a
portrait 1080x2340 image to a 2340x1080 display.

Underneath it, something worse: the session **restarted every ~20 seconds, forever**. On the
host that shows up as the touch device being recreated over and over:

```
$ journalctl --user --since -10min | grep -c droppix-touch
7
kwin_wayland: Libinput: event29 - droppix-touch-27001-pen: ... (02:08:04)
kwin_wayland: Libinput: event29 - droppix-touch-27001-pen: ... (02:08:24)
kwin_wayland: Libinput: event29 - droppix-touch-27001-pen: ... (02:08:43)
```

## Root cause

`stream_daemon.cpp` normalised the HELLO dimensions to the reported orientation with a
**one-way** swap:

```cpp
if (orientation_is_portrait(ocode) && w > h) std::swap(w, h);
```

Its comment stated the assumption out loud: *"The app always sends landscape HELLO dims
(1920x1080)."* That held for the landscape-natural tablet it was written for. It is false for
a portrait-natural phone:

- `StreamActivity` computes the dims once in `startStreaming()` from `getRealMetrics`.
- The manifest sets `android:configChanges="orientation|screenSize|keyboardHidden"`, so the
  Activity is **not** recreated on rotation and those dims are never recomputed.

So the phone sends **portrait dims with a landscape orientation code** — the mirror image of
the handled case, which fell straight through unswapped.

That alone gives the black bars. The restart loop follows from it, because the orientation
handler restarts the session whenever the reported shape disagrees with the built one:

```cpp
const bool cur_portrait = h > w;                                  // true: 1080x2340
if (!cfg_.mirror && orientation_is_portrait(code) != cur_portrait) // landscape != portrait
  restart_for_orientation = true;
```

Every reconnect re-sent the same unswapped dims, the host rebuilt the same portrait source,
the phone re-reported landscape, and it restarted again. The existing comment even warned
about this exact non-settling loop — the guard against it had only ever been written for one
of the two directions.

## Fix

Normalise both ways, in a pure helper (`orientation.h`), so the built shape always agrees
with the authoritative orientation:

```cpp
inline void orient_dims(int code, int& w, int& h) {
  const bool want_portrait = orientation_is_portrait(code);
  if (want_portrait == (h > w)) return;   // already right (square: nothing to do)
  std::swap(w, h);
}
```

The orientation is authoritative and the dimensions are not: the tablet knows how it is being
held, while its dims are whatever the app measured whenever the session happened to start.

## How to detect this in the future

**A session that restarts on a rhythm is a disagreement that never resolves**, not a flaky
link. Count the restarts before assuming USB or the network:

```bash
journalctl --user --since -10min | grep -c droppix-touch   # one per session start
```

And when normalising a value against an authority, assert the post-condition the consumer
tests — here, that `h > w` matches `orientation_is_portrait(code)` for every code and every
input shape (`OrientDims.ResultAgreesWithTheRestartPredicate`). A one-directional `if` looks
complete in review; the round-trip property does not.

Related: any client-reported geometry is a snapshot. `configChanges` (or any "don't recreate
me" flag) means the client can report values that were true only at launch.
