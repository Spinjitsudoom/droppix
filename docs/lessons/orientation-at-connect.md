# L-2026-11-25-orientation-at-connect: the tablet reported "natural" orientation at connect

- **ID:** `L-2026-11-25-orientation-at-connect`
- **Tags:** `android`, `orientation`, `wrong-answer`, `medium`, `lesson`
- **Date:** 2026-11-25

## Symptom

(Found reviewing the orientation path at the user's request.) A tablet already held in
portrait when the stream started reported landscape in HELLO. The host built a landscape monitor,
the sensor then caught up, and the session restarted. A tablet lying **flat** never corrected:
`OrientationEventListener` reports "unknown" when flat, so the mapper stayed on its initial
quarter (landscape), and the host streamed a landscape monitor to a tablet displaying portrait.
With **rotation lock** on, the sensor still moved the mapper, so a reconnect reported the
physical orientation instead of the locked one the tablet was showing.

## Root cause

`OrientationMapper` only reports *changes* from its last settled quarter, which starts at 0
("natural"). Nothing initialised it from the display's real rotation.

## Fix

- `OrientationMapper.seedFromDisplayRotation(rotation)`, called in `startStreaming()` before
  HELLO. Display `ROTATION_90` = sensor angle ~270, so quarter = (4 − rotation) mod 4.
  Unit-tested against the sensor path for every rotation.
- With rotation lock on, the sensor is ignored (the display, and so the stream, keeps its shape).

The host side was verified end to end in a Plasma 6 VM. A portrait connect builds 800×1280, and
a mid-session rotation restarts the stream at 1280×800. Touch, right-click and pen land
correctly in portrait.
