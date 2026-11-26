# L-2026-11-26-qt-click-on-touch-cancel: a two-finger tap also clicked under the first finger

- **ID:** `L-2026-11-26-qt-click-on-touch-cancel`
- **Tags:** `host`, `input`, `kwin`, `wrong-answer`, `high`
- **Date:** 2026-11-26

## Symptom

A two-finger tap opened the context menu, but in QtWidgets apps (Dolphin, Kate, System Settings…)
it first clicked whatever was under the first finger. A fullscreen `QPushButton` counted one click
per two-finger tap. GTK did not.

## Root cause

The tap's fingers reached the desktop as ordinary touches. Qt turns the first touch into a mouse
press and its end into a release: a click. Cancelling the touch instead (contact becomes
`MT_TOOL_PALM`, which libinput reports as a touch cancel) does **not** help: Qt deliberately
synthesizes a mouse release for a cancelled touch too (`processTouchEvent`, `TouchCancel`).

## Fix

`TouchGate` (host): a new touch is held back until it is clearly not a two-finger tap. A tap is
delivered whole when it lifts (clicks happen on release anyway, so it is not later); a moving touch
is released at once, every finger starting where it went down; a still one after 100 ms (two
fingers: the 400 ms tap time). A two-finger tap is never delivered, only the right-click. The palm
cancel stays for fingers already released (QML and GTK honour it).

Verified on the Nexus 10: Qt button 1 click / 0 for three two-finger taps / 1; drags, scrolls and
taps land on the exact pixel.

## Takeaway

Test touch gestures against a QtWidgets app, not only a GTK probe or the desktop background.
