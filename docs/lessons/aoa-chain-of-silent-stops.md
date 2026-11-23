# L-2026-11-24-aoa-chain-of-silent-stops: AOA "only works with USB debugging" was four separate stops

- **ID:** `L-2026-11-24-aoa-chain-of-silent-stops`
- **Tags:** `host`, `gui`, `android`, `transport`, `evdi`, `silent-failure`, `high`
- **Date:** 2026-11-24

## Symptom

On the Nexus 10, droppix worked over the adb route but never over AOA. With USB debugging
off the tablet was "not even detected". With it on, the cable switched into accessory mode
but no monitor ever appeared: the tablet sat on a black stream screen.

## Root causes (each one alone was enough to stop it)

1. **The tablet had no USB function without adb.** With debugging on it enumerated as
   `18d1:4ee7`, Google's "debug only" ID: MTP and PTP were both unticked (Settings →
   Storage → ⋮ → USB computer connection). Turning adb off left no function at all, so the
   tablet dropped off the bus (`USB disconnect`, no re-enumeration) and the host had nothing
   to send the AOA handshake to. No app can change this setting. With MTP ticked it
   enumerates as `18d1:4ee1` and the handshake works with debugging off.
   Check: `lsusb` while debugging is on. `4ee7` / `4ee3` mean nothing will be left once adb is off.
2. **The app only adopted the accessory at plug-in.** `StreamActivity` reads the accessory
   solely from the `USB_ACCESSORY_ATTACHED` intent. Opened any other way with the cable
   already in (launcher, after a kill), it never looked, and the host's AOA streamer waited
   forever. `ConnectActivity.adoptAttachedAccessory()` now checks
   `UsbManager.accessoryList` on resume, requesting permission if needed.
3. **An AOA session could take the Server's port.** Device sessions and the Server listener
   shared one allocator, so whichever started first got the base port. An AOA session
   (which uses no TCP port) taking 27000 moved the listener to 27001, while the mDNS advert,
   the adb tunnel and the client's USB button all still pointed at 27000. The tablet logged
   `connect/stream failed: Connection closed by peer` in a loop. Device sessions now skip
   the base port while the Server is on (`allocate_port(..., reserve_base)`).
4. **The evdi module was not loaded.** `kmod-evdi` installs it but nothing loads it at boot,
   and `evdi_add_device()` needs `/sys/devices/evdi`. The root streamer now runs
   `modprobe evdi` when that is missing (`ensure_evdi_module()` in `virtual_display.cpp`).

## How to diagnose next time

- `lsusb | grep 18d1`: `2d00`/`2d01` means the handshake worked and it's waiting on the tablet;
  anything else means it never got that far.
- `journalctl -k | grep 'usb 1-'` shows whether the device re-enumerates or vanishes.
- `adb logcat | grep droppix` (when debugging is on) shows which route the app took:
  `connecting to 127.0.0.1` is the adb route, `aoa: streaming` is the cable.
- `lsmod | grep evdi` before blaming the stream for a missing monitor.
