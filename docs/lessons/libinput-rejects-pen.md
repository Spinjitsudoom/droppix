# L-2026-11-25-libinput-rejects-pen: libinput ignored droppix's stylus device on every desktop

- **ID:** `L-2026-11-25-libinput-rejects-pen`
- **Tags:** `host`, `input`, `pen`, `silent-failure`, `high`, `lesson`
- **Date:** 2026-11-25

## Symptom

Found while testing GNOME in a VM. A stylus stroke from the client produced nothing on screen, while
touch on the same session worked. The journal said:

```
libinput error: event6 - droppix-touch-27000-pen: libinput bug: missing tablet capabilities: btn-stylus resolution. Ignoring this device.
```

## Root cause

libinput, which both Mutter and KWin use, rejects a tablet device unless it declares `BTN_STYLUS`
and a non-zero resolution on `ABS_X`/`ABS_Y`. The droppix pen uinput device had `BTN_TOOL_PEN`,
`BTN_TOOL_RUBBER`, `BTN_TOUCH` and 0..65535 axes with no resolution. The device existed in
`/dev/input`, so nothing looked wrong, but no compositor ever saw it.

The Nexus 10 has no active stylus, so on-device testing never exercised the pen. The "Stylus"
row in STATUS was marked Shipped without a pen ever reaching a compositor.

## Fix

The pen's capabilities moved into a pure, tested `pen_device_caps()` (`host/src/input_caps.*`):
it adds `BTN_STYLUS` and declares 262 units/mm on X/Y. The compositor maps the device onto the
droppix output, so the exact figure doesn't matter, only that it's non-zero. Verified on GNOME 46:
a stroke at 75%/75% lands at exactly 75%/75% of the droppix screen.

## How to detect this in the future

- `sudo libinput list-devices | grep -A3 droppix` must list the pen. A rejected device prints
  the "missing tablet capabilities" error instead.
- Unit test `PenCaps.MeetsLibinputTabletRequirements` pins the requirements.
