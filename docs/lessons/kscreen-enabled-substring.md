# L-2026-11-24-kscreen-enabled-substring: a disabled monitor counted as enabled, so right-clicks opened on another screen

- **ID:** `L-2026-11-24-kscreen-enabled-substring`
- **Tags:** `host`, `desktop-backend`, `input`, `wrong-answer`, `high`
- **Date:** 2026-11-24

## Symptom

The two-finger-tap right-click opened its context menu on a different monitor. Vertically
it was right; horizontally it was at about 70% of the correct x.

## Root cause

`parse_kscreen_outputs()` set `enabled = true` for any line in an output's block that
contained "enabled" but not "disabled". A **disabled** output in Plasma 6 still lists
`HDR: enabled` and `Wide Color Gamut: enabled`, so it was turned back on. The disabled
1920-wide DP-3 then widened `desktop_bounds()` from 2688 to 3840, and the right-click
pointer was created with `ABS_X` max 3839. KWin scales that absolute range onto its real
2688-wide workspace, so every x came out as x × 0.7.

A first fix (`29d7e8f`, make `desktop_bounds` skip disabled outputs) was correct but had no
effect, because the parser never reported DP-3 as disabled. Its test sample had no HDR
lines.

## Fix

Only the output's bare `enabled` / `disabled` line sets the state. The regression test feeds
real `kscreen-doctor -o` output, colour codes and HDR lines included.

## Diagnose

- `evtest /dev/input/<droppix-rightclick>`: check the header's `ABS_X` Max against the real
  desktop width (`spectacle -b -n -f` gives the screenshot size).
- `kscreen-doctor -o | cat -A`: look at the raw lines the parser sees.
- On Android 5.1 the `adb shell` user is in the `input` group, so
  `sendevent /dev/input/event2 ...` can produce a real two-finger tap for testing.
