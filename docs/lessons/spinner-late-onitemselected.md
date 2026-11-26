# G-2026-11-26-spinner-late-onitemselected: filling a Spinner fires its listener later

- **ID:** `G-2026-11-26-spinner-late-onitemselected`
- **Tags:** `android`, `gui`, `gotcha`, `medium`
- **Date:** 2026-11-26

## Symptom

The first time the in-stream settings panel opened, the stream restarted.

## Root cause

`seed()` set each Spinner with `setSelection()` inside a `seeding = true` guard. Android delivers
`onItemSelected` on the next layout pass, after the guard is already false again, so the
Resolution spinner's listener ran as if the user had picked a resolution: reconnect.

## Fix

Decide from values, not from which control fired: `PanelChange.of(stored, shown)` returns NONE,
DISPLAY, STREAM_PARAMS or RESOLUTION, and a late callback carrying the stored value is NONE.

## Takeaway

A guard flag around `setSelection()` does not suppress Spinner callbacks. Compare against the
stored value.
