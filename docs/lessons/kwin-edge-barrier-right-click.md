# L-2026-11-25-kwin-edge-barrier-right-click: Plasma 6's edge barrier ate 100 px of the first right-click

- **ID:** `L-2026-11-25-kwin-edge-barrier-right-click`
- **Tags:** `host`, `input`, `kwin`, `wrong-answer`, `medium`, `lesson`
- **Date:** 2026-11-25

## Symptom

On Plasma 6 (Ubuntu 25.10 VM, KWin 6.4), the first two-finger right-click of a session opened
100 px left of the tap. Later clicks on the same screen were exact. Plasma 5 and GNOME were
exact every time.

## Root cause

Plasma 6 added an **edge barrier** between screens (default 100 px, `kwinrc [EdgeBarrier]`).
KWin applies it to absolute pointer motion too. The right-click pointer jumped from wherever the
real mouse was (the main screen) straight to the target on the tablet, so crossing the screen edge
cost 100 px. A target within 100 px of the tablet's edge never left the main screen at all.
Setting `EdgeBarrier=0` made the first click exact, which confirmed the cause.

## Fix

`ClickSchedule` gained a **Land** step: the pointer first jumps to the droppix output's centre,
so the 100 px loss happens on the tablet. 15 ms later it wiggles and moves to the target, a move
within one screen that the barrier ignores. Then it presses (+30 ms) and releases (+80 ms).

Verified on Plasma 6: first click at (320,200) exact, and a click 25 px from the tablet's edge
exact. Plasma 5 is unchanged and exact.

## Note

The VMs auto-lock after idle, and the lock screen swallows all input (no probe events at all).
`kscreenlockerrc [Daemon] Autolock=false` in test VMs.
