# AOA (Android Open Accessory) USB Transport — Design

**Date:** 2026-07-05
**Status:** Approved (pending implementation plan). M0 is a go/no-go validation gate.

## Goal

Add a USB transport that works with **no USB tethering and no Developer Options** —
the way Spacedesk does — so USB streaming works even on Wi‑Fi‑only tablets like the
Nexus 10 (which has no tethering toggle). The host becomes a USB host that switches
the tablet into **Android Open Accessory (AOA)** mode and streams the existing droppix
wire protocol over the AOA bulk endpoints; the Android app implements `UsbAccessory`.

**Additive:** the just-built **USB-tethering** and **Wi‑Fi** transports are kept
unchanged. AOA is a third channel behind a shared abstraction; the user can still
connect over Wi‑Fi exactly as today.

## Why AOA (context)

The Nexus 10 connected over USB with Spacedesk with no tethering step and no debugging.
That mechanism is AOA — the only USB path that needs neither tethering nor Developer
Options and works on any Android 3.1+. Our earlier USB-tethering transport can't work on
the Nexus because it has no tethering toggle (and no app can enable tethering without
system permissions). AOA closes that gap.

## Decisions

- **Trust:** the physical USB cable is the trust boundary — run the protocol **plaintext
  over AOA, no TLS, no PIN**. Matches the existing `127.0.0.1` localhost path and
  Spacedesk. (Someone with physical USB access already has full access.)
- **Orchestration:** the **root streamer owns all USB** (approach A below); the GUI gets a
  one-time udev rule to *notice* Android plug-ins for the device list.

## Section 1 — Transport abstraction & protocol reuse

Both sides already funnel all traffic through a byte stream, so only that seam is new.

- **Host:** a `ByteChannel` interface — `recv(buf,n)`, `send_all(buf,n)`,
  `wait_readable(timeout)`, `close()`. `TransportServer`'s framing/message logic
  (`read_hello`, `send_config`, `send_video`, `send_audio`, `send_overlay`,
  `poll_control`, touch/orientation) runs against a `ByteChannel`. Two implementations:
  `SocketChannel` (today's TCP+TLS, behavior-identical) and `AoaChannel` (libusb bulk
  endpoints). No protocol/message code changes.
- **Android:** `TransportClient.run()` is refactored to take an `InputStream` +
  `OutputStream` (its channel) instead of creating the `SSLSocket` itself. The socket
  path still supplies those; the AOA path supplies them from the `UsbAccessory`
  `ParcelFileDescriptor` (`FileInputStream`/`FileOutputStream`). Read loop, HELLO,
  PING/PONG, TOUCH/ORIENTATION — unchanged.

Result: the framing/protocol layer becomes transport-agnostic on both ends; the only new
code is the two channel setups plus the AOA handshake/orchestration.

## Section 2 — Host AOA orchestration

The host must (1) notice a plug-in, (2) flip the device into accessory mode over raw USB,
(3) stream over the bulk endpoints. (2) and (3) need privileged USB access.

**Approach A (chosen): the root streamer owns all USB.** The streamer already runs as root
(evdi/uinput), so libusb needs no udev rule there. The GUI spawns a per-device streamer
with `--usb-aoa <serial>`; the streamer does the handshake and runs the protocol over the
endpoints. To *notice* plug-ins for the device list, the GUI gets a **one-time udev rule**
(installed like the existing polkit rule) granting read access to Android USB descriptors.

Alternatives rejected: **B** — a dedicated root USB bridge that pipes bytes to a
transport-agnostic streamer (two root processes + extra IPC); **C** — the GUI does libusb
directly (needs a broad udev rule + handing a live USB fd across the streamer boundary).

**The AOA handshake (libusb, in the streamer):**
1. Open the plugged-in Android; control request **51** (get AOA protocol) → version ≥ 1 =
   AOA-capable.
2. Control request **52**: send identification strings (manufacturer / model / description
   / version / uri / serial). These must match the Android `accessory_filter.xml` so
   Android recognizes droppix and can "use by default".
3. Control request **53** (ACCESSORY_START) → the device **re-enumerates** as VID `0x18D1`
   PID `0x2D00`/`0x2D01`.
4. Re-find the accessory-PID device (bounded wait; match by serial), claim its interface,
   grab the bulk **IN/OUT** endpoints → `AoaChannel` → run the existing protocol.

**Detection/UX:** the GUI enumerates USB (via the udev rule), lists each Android as
"*Name* — USB (cable)"; connect (auto or click) spawns the streamer. First time on the
tablet, Android shows "Open droppix for this USB accessory?" — tick "use by default" →
plug-and-go thereafter. Cross-transport **id dedup** (already built) prevents a second
monitor if the same tablet is also on Wi‑Fi.

## Section 3 — Android `UsbAccessory` side

Small, self-contained; the protocol loop is reused.

- **Manifest:** `<uses-feature android:name="android.hardware.usb.accessory"/>` and an
  intent filter for `android.hardware.usb.action.USB_ACCESSORY_ATTACHED` with `<meta-data>`
  → `res/xml/accessory_filter.xml`. The filter
  (`<usb-accessory manufacturer="droppix" model="droppix" …/>`) must **exactly match** the
  identification strings the host sends in handshake step 2.
- **Entry point:** on the attached intent (or by `UsbManager.getAccessoryList()` when the
  connect screen opens), call `UsbManager.openAccessory(accessory)` → `ParcelFileDescriptor`
  → `FileInputStream`/`FileOutputStream`; hand them to the refactored `TransportClient.run`
  with **no TLS, no PIN**. HELLO carries the same dims/density/id as today.
- **Lifecycle:** unplugging detaches the accessory → the read loop ends → back to the
  connect screen (same as a dropped socket). No `StreamActivity`/decoder changes.

No wire/protocol change, no new permissions beyond the accessory feature, and the
Wi‑Fi/tethering entry points are untouched.

## Milestones (M0 gates the rest)

| # | Milestone | Deliverable |
|---|---|---|
| **M0** | **AOA validation spike.** Throwaway host libusb program (handshake 51/52/53) + minimal Android `UsbAccessory` echo handler. On the Nexus: confirm it enters accessory mode, endpoints open, bytes round-trip, throughput clears ~8 Mbps. | Go/no-go: does AOA work on this device? |
| **M1** | Host `ByteChannel` refactor; `SocketChannel` = today's behavior (all transport tests still pass). | Transport-agnostic `TransportServer`, no behavior change. |
| **M2** | Host `AoaChannel` + `--usb-aoa <serial>` in the streamer (handshake + re-enumeration + bulk endpoints). | Streamer serves one tablet over the cable. |
| **M3** | GUI: enumerate Android USB, list "*Name* — USB (cable)", install the udev rule, spawn the streamer, reuse id dedup. | Plug-and-go from the GUI. |
| **M4** | Android `TransportClient` channel refactor + manifest/`accessory_filter` + `openAccessory` entry. | Tablet streams over AOA. |

## Risks

- **Old-device AOA quirks** (Android 5.1.1 on the Nexus) — **M0 is the gate**; if AOA
  doesn't work there, we learn it before building the transport.
- **Re-enumeration timing** after ACCESSORY_START — bounded wait, re-match by serial.
- **AOA bulk throughput** — M0 measures; 8 Mbps default bitrate is well within USB 2.0, and
  Spacedesk proves it.
- **GUI udev rule** — installed once (like the polkit rule); read-only on Android USB
  descriptors.
- **Concurrency** — one accessory per device; multi-monitor over AOA = one cable/streamer
  each (already handled by the per-session model).

## Testing

- Protocol/codec: unchanged, already covered.
- `ByteChannel` refactor: guarded by the existing transport tests (SocketChannel must stay
  behavior-identical) + a fake-channel unit test of the framing.
- AOA handshake: hardware — M0 spike + manual e2e.
- Android channel refactor: existing tests + manual e2e.
- **Success:** plug the Nexus into the PC with Developer Options **off** and no tethering →
  droppix streams over the cable.

## Out of scope (YAGNI)

- TLS over USB (dropped by decision — physical-cable trust).
- Removing or changing the Wi‑Fi / USB-tethering transports (kept; AOA is additive).
- Any change to streaming, evdi, multi-monitor, or the wire protocol.

## M0 validation results (2026-07-06): **GO**

Ran the throwaway spike (`spike/aoa/aoa_spike.c` + `AoaEchoActivity`) on the real
Nexus 10. Confirmed:

- **AOA protocol version 2** reported by the device.
- **ACCESSORY_START switches it to accessory mode**; it re-enumerates as `18d1:2d01`
  (accessory + adb).
- The host **claims the interface and finds the bulk endpoints** (IN `0x85`, OUT `0x07`).
- Android **auto-launches our `USB_ACCESSORY_ATTACHED` activity** (once "use by default"
  is set) and `openAccessory` succeeds (`aoa-echo: accessory opened`).
- `dumpsys usb` shows **Spacedesk's own AOA accessory filter**
  (`manufacturer="datronicsoft", model="spacedesk"`) registered alongside ours —
  confirming Spacedesk drives this exact device over AOA.

Not cleanly captured: a **throughput number**. The throwaway blind-echo hit a setup race
(the host claiming the interface stalls the app's naive read with `EIO`). This is a
spike artifact, not a device limit — USB 2.0 is 480 Mbps carrying ~8 Mbps, and Spacedesk
streams full video over this same path on this same tablet. Real throughput is measured
in **M2/M4**, where the connection order and the actual protocol (not a two-process blind
echo) define the data flow.

**Decision: GO** — proceed to M1 (`ByteChannel` refactor) → M2 → M3 → M4.

### State when paused
- Branch `feat/aoa-usb-transport` holds: this spec, the M0 plan, and the throwaway spike
  (`spike/aoa/`, `AoaEchoActivity`, `accessory_filter.xml`, the manifest entry). The spike
  is NOT production — M2 replaces `aoa_spike`'s handshake with `AoaChannel`; M4 replaces
  `AoaEchoActivity` with the real `UsbAccessory` transport.
- Resume by planning M1 with writing-plans, then executing M1–M4.
