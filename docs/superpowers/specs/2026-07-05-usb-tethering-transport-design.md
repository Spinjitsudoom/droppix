# USB Tethering Transport (no USB debugging) — Design

**Date:** 2026-07-05
**Status:** Approved (pending implementation plan)

## Goal

Let droppix connect over USB **without Android USB debugging / Developer Options** —
the way Spacedesk does. The tablet uses **USB tethering** (a normal Settings toggle);
droppix reaches it over the resulting USB-local IP link and reuses its existing
network transport. The old adb-based USB path is **removed entirely**.

Bonus: dropping adb also removes the flaky `adb reverse` behavior seen on the old
Nexus (Android 5.1.1).

## Chosen approach

**USB tethering + reuse the IP transport** (chosen over an AOA raw-USB channel, which
would be a whole new transport on both ends with real video-over-bulk latency risk).
**Replace** the adb USB path rather than keep it alongside.

## Connection model

A USB-tethered tablet is an **IP device reachable over the USB link**, so the entire
network path — TLS, PIN pairing, WAKE, H.264 streaming, multi-monitor, auto-connect —
is reused unchanged.

- The user enables **USB tethering** on the tablet and **opens the droppix app** (no
  adb means the host cannot launch it — same as Wi-Fi).
- Over USB, the tablet is the gateway (typically `192.168.42.129`) and the PC is a
  DHCP client on the same subnet; they reach each other directly.
- Connect uses the **existing host-initiated flow**: host → `WAKE(streamPort)` to the
  tablet's tether IP → tablet dials the PC's tether IP:streamPort → TLS + (first time)
  PIN → stream. The already-merged **auto-connect** works over this untouched: a paired
  tethered tablet auto-connects when discovered.

The only capability that does not carry over from Wi-Fi is **discovery** — Android mDNS
is unreliable across the USB-tether interface — so that is the one genuinely new piece.

## Discovery over the USB link

A small **UDP probe/reply discovery**, mDNS-independent and point-to-point:

- **Host `TetherScanner`** (replaces the adb-based `UsbClientScanner`): every ~2 s it
  opens a UDP socket, enables broadcast, sends a **probe** datagram to each non-loopback
  interface's broadcast address on a fixed droppix discovery port, waits a short window
  (~500 ms), and collects **reply** datagrams. Over USB tethering the broadcast reaches
  the tablet (the gateway); on Wi-Fi it harmlessly reaches nothing new.
- **Tablet responder** (a sibling of the existing `WakeService`, bound while the connect
  screen is active): listens on the fixed discovery port and answers each probe with a
  reply carrying `{device id, display name, wake port}`, sent to the probe's source.
- **Host** learns the tablet's tether IP from the reply's source address, yielding the
  full tuple the rest of the pipeline consumes: `{id, name, address, wakePort}`. It
  drops that into the **existing device list + auto-connect + WAKE-connect path**; the
  `id` feeds the paired-eligibility check already built for auto-connect.

### Wire format

Two new datagrams, magic-prefixed and length-checked like the existing `WAKE`, with
**byte-identical C++↔Kotlin codecs locked by shared test vectors** (project pattern).
Fixed discovery UDP port: **27010** (distinct from the allocatable stream ports
27000–27003).

- **Probe (host → tablet):** `"DPXQ"` (4 ASCII bytes). No payload.
- **Reply (tablet → host):** `"DPXR"` (4) · `u16 wakePort` (big-endian) · `u8 idLen` ·
  `id[idLen]` · `u8 nameLen` · `name[nameLen]`. `id` = `DeviceIdentity.stableId`;
  `name` = `DeviceIdentity.displayName`. A decoder rejects a datagram whose declared
  lengths exceed its bounds.

## Removals (the whole adb USB path)

- `host/gui/usb_client_scanner.{h,cpp}` and `host/src/usb_scan.{h,cpp}`
  (`adb devices` → `pm list packages` → `getprop`) → replaced by `TetherScanner`.
- `AdbManager`'s USB pieces (`usbConnect` = adb reverse + `am start`, `setupReverse`,
  the `adb devices` status refresh) — and `AdbManager` itself if nothing else uses adb.
- The streamer's `--adb-reverse` flag and the `adb reverse` call in `stream_main.cpp`.
- Tablet: the USB-icon one-tap (`127.0.0.1:27000` over the reverse tunnel) and the
  `usb_autoconnect` / `usb_port` launch-intent handling in `ConnectActivity`.
- The **adb branch** of the merged `connectDevice` / `evaluateAutoConnect` code.

## GUI representation

A tethered tablet connects via WAKE, so mechanically it is the **net path**. It appears
in the device list labeled "*Name* — USB" but is keyed by its tether IP and connects
exactly like a Wi-Fi device. The auto-connect policy, the shared `connectDevice`, the
Active-monitors panel, and multi-monitor all handle it with **no new connect logic** —
only the discovery source (tether probe vs mDNS) and the display label differ. The old
`"usb"` transport tag collapses into `"net"` plus a label.

## Cross-transport dedup by device id

A tablet reachable at both its Wi-Fi IP and its tether IP at the same time must not spin
up **two** monitors. Today a session is keyed by address, which would allow it. Fix:
**dedup by `device id`** — if a tablet's id already has an active session (via either
transport), neither manual connect nor the auto-connect policy opens a second.

- The `id` is available at discovery on **both** transports now (mDNS TXT for Wi-Fi —
  already added; the reply datagram for tether).
- Thread the id from the device-list item through to the `Session`, and dedup on it:
  - Manual connect of a device whose id already has a session → blocked (informational).
  - `AutoConnectPolicy` → exclude a candidate whose id is already an active session's id.
- This deliberately touches the just-merged auto-connect code; it is a small, scoped
  follow-on, not a surprise.

## Route hygiene

USB tethering can install a default route through the tablet and hijack the PC's
internet. The app does **not** reconfigure networking; instead this is a documented
setup step (NetworkManager: mark the USB connection `ipv4.never-default` / higher
metric), folded into the first-run setup script. Noted as a caveat, not auto-fixed
(YAGNI).

## Testing

- **Pure codec tests** for probe + reply, byte-identical C++↔Kotlin via shared test
  vectors (same discipline as the `WAKE` codec): encode produces exact bytes; decode
  round-trips; decode rejects bad magic and out-of-bounds lengths.
- **Host:** unit-test the pure parts of `TetherScanner` — building a probe and parsing a
  reply into `{id, name, wakePort}` (source address comes from the socket, tested
  manually). Interface enumeration / socket I/O is integration.
- **Host dedup:** unit-test the id-based dedup in `AutoConnectPolicy` / session lookup
  (candidate with an already-active id is excluded).
- **Tablet:** unit-test the reply builder (mirrors `WakeTest`).
- **Manual e2e:** USB tethering on → open the app → tablet appears as "— USB" →
  auto-connects → monitor streams, with Developer Options **off**; and Wi-Fi + USB at
  once yields exactly **one** monitor.

## Out of scope (YAGNI)

- The AOA raw-USB transport (rejected approach).
- Auto-managing the tablet's tethering toggle or the PC's routing (documented, not
  automated).
- Any change to the streaming, TLS, pairing, or multi-monitor layers — all reused.
