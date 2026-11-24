# L-2026-11-24-ime-auto-shows-on-tablet: the keyboard opened by itself and ate half the touches

- **ID:** `L-2026-11-24-ime-auto-shows-on-tablet`
- **Tags:** `android`, `input`, `wrong-answer`, `high`
- **Date:** 2026-11-24

## Symptom

Touch "worked, but not 100%". Every time the stream screen opened, the soft keyboard
popped up over half the tablet. Taps in that half typed letters instead of reaching the PC,
and the navigation bar came back (window flags `0x1700`, hide bits cleared), covering the
bottom edge of the stream.

## Root cause

`GlDisplayView` reports `onCheckIsTextEditor() = true` (so the keyboard button can type
into it) and takes focus in `onResume`. `StreamActivity` called
`setSoftInputMode(SOFT_INPUT_ADJUST_NOTHING)`, which replaces the whole mode and leaves
the *state* unspecified. On a large screen Android then shows the IME automatically for a
focused text editor.

## Fix

Add `SOFT_INPUT_STATE_ALWAYS_HIDDEN`. The keyboard button's explicit `showSoftInput` still
opens it. The activity also re-applies immersive mode whenever the hide flags are dropped,
since closing the keyboard doesn't change window focus.

## Diagnose

`adb shell dumpsys input_method | grep mInputShown` and
`adb shell dumpsys window | grep mLastSystemUiFlags` (`0x1706` = bars hidden).
