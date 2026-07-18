# AOA M3: GUI USB Detection + Auto-Spawn `--usb-aoa` — Design

**Status:** Shipped on master.
**Branch:** `feat/aoa-m3-gui-usb` (off `master`)
**Depends on:** AOA transport (M1/M2/M4), merged to `master` (commit `5e71291`).

## Goal

Make the AOA USB transport usable from the host GUI: automatically detect an
Android tablet plugged in over USB that can run droppix over AOA (no adb, no USB
debugging), show it in the existing "Available clients" list, and on Connect
(or auto-start) spawn the streamer with `--usb-aoa <serial>` through the
existing pkexec-root path. **Host/GUI only — no Android or wire-protocol
change.**

## Non-Goals / Out of Scope

- The AOA transport itself — DONE and merged (`droppix_stream --usb-aoa`, the
  Android `StreamActivity` accessory path).
- **A udev rule.** Detection reads world-readable sysfs as the normal user, and
  the streamer already runs as root via pkexec (root can do AOA), so no udev
  rule is needed for the product. (The `99-droppix-aoa.rules` file was only a
  developer convenience for running the streamer as a non-root user during
  iteration; it is not part of this milestone.)
- No changes to the Android app, the wire protocol, or TLS/PIN (the physical
  cable remains the trust boundary for AOA).

## Context (current code, verified 2026-07-06)

- The GUI's "Available clients" list is built by `MainWindow::rebuildClientList()`,
  which merges `netDevices_` (mDNS, `MdnsBrowser`) and `tetherClients_`
  (`TetherScanner`) into `devicesList_`. Each row carries `Qt::UserRole` =
  transport, `+1` = ident/address, `+2` = wake port, `+3` = device id.
- `onConnectToSelectedDevice()` → `connectDevice()` → `startSession()`.
  `startSession()` builds the command via `build_command(s, streamBin_, port,
  touchName)`, spawns a `StreamController`, adds an "Active monitors" row, and
  calls a `directTablet` lambda (for net/tether it sends a `DPXW` wake datagram).
- Auto-connect (`evaluateAutoConnect()`) already exists for network devices; it
  only targets **approved** ids (`ApprovedStore`) when the auto-connect toggle
  is on.
- `build_command` (`host/gui/args_builder.{h,cpp}`) has **no** AOA awareness yet.
- The normal user can read a plugged Android's `idVendor`, `idProduct`,
  `product`, `manufacturer`, and **`serial`** directly from
  `/sys/bus/usb/devices/*/` (confirmed: Nexus 10 → serial `R32D204ZH6J`).

## Architecture

A sysfs-polling scanner feeds device rows into the existing list; a USB-AOA
branch in the connect path spawns the root streamer with `--usb-aoa <serial>`;
a small serial store gates auto-start on plug.

```
/sys/bus/usb/devices/*  ──parse──▶  aoa_scan (pure)  ──▶  AoaScanner (QObject, 2s timer)
                                                                │ clientsChanged(QList<AoaClient>)
                                                                ▼
                              MainWindow: aoaClients_ ─▶ rebuildClientList()  (adds "<product> — USB" rows)
                                                                │  Connect / auto-start
                                                                ▼
                              connectDevice()/startSession() ─▶ build_command(... --usb-aoa <serial>)
                                                                ▼
                              pkexec droppix_stream --usb-aoa <serial>  (root; switches device to accessory)
                                                                │  first CONFIG
                                                                ▼
                              AoaKnownStore.add(serial)   (enables future auto-start)
```

### Detection strategy

List a USB device iff its vendor ID is in a **known-Android-OEM allowlist**
(the set adb's udev rules ship: Google `18d1`, Samsung `04e8`, Xiaomi `2717`,
OnePlus `2a70`, Motorola `22b8`, Huawei `12d1`, LG `1004`, Sony `0fce`, Asus
`0b05`, HTC `0bb4`, etc.) **or** the device is already in accessory mode
(`18d1:2d00` / `18d1:2d01`). Hubs, HID, and mass-storage devices are excluded.
This is safe (never disturbs non-Android USB), needs no root, and is trivially
extensible if a device's vendor is missing. Rejected alternatives: "optimistic"
(lists non-Android gadgets); "active AOA probe" (needs root/libusb, pokes other
devices).

## Components

### `host/src/aoa_scan.{h,cpp}` (pure, primary test target)

```cpp
namespace droppix {

struct AoaDevice {
  std::string serial;        // USB iSerialNumber (matches --usb-aoa)
  std::string product;       // sysfs "product" (display name), may be empty
  uint16_t    vendor_id = 0;
  uint16_t    product_id = 0;
  bool        accessory_mode = false;   // idProduct is 0x2d00/0x2d01
};

// True if vendor_id is a known Android OEM.
bool is_android_vendor(uint16_t vendor_id);

// Enumerate USB devices under `sysfs_root` (default "/sys/bus/usb/devices"),
// returning AOA candidates: known-vendor OR accessory-mode, excluding
// hubs/HID/storage. Reads idVendor/idProduct/serial/product/bDeviceClass and
// the interface bInterfaceClass files. Missing/unreadable files are skipped.
std::vector<AoaDevice> parse_usb_sysfs(const std::string& sysfs_root = "/sys/bus/usb/devices");

}  // namespace droppix
```

- No Qt, no root, no libusb — plain file reads, so tests drive it against a
  fake sysfs tree built in a temp directory.
- Exclusion: skip a device whose `bDeviceClass` is `09` (hub); skip if its only
  interface class is HID (`03`) or mass-storage (`08`). (Accessory-mode and
  allowlisted phone/tablet devices pass.)

### `host/gui/aoa_scanner.{h,cpp}` (thin QObject)

```cpp
struct AoaClient { QString serial; QString product; bool accessoryMode = false; };

class AoaScanner : public QObject {
  Q_OBJECT
 public:
  explicit AoaScanner(QObject* p = nullptr);
  bool available() const { return true; }
  void start();  // start the 2s timer + emit immediately
  void stop();
 signals:
  void clientsChanged(QList<droppix::AoaClient> clients);
 private:
  void tick();   // parse_usb_sysfs() -> map to AoaClient -> emit
  QTimer timer_;
};
```

- 2 s cadence, matching `TetherScanner`/`MdnsBrowser`. Emits every tick (the
  list is small; MainWindow rebuilds idempotently).

### `host/gui/aoa_known_store.{h,cpp}` (serial set)

- File-backed set of USB serials at `~/.config/droppix_gui/known_aoa` (one
  serial per line), the USB analogue of `ApprovedStore`.
- API: `bool contains(serial) const;` `void add(serial);` `QStringList all() const;`
- Round-trip tested (mirrors `ApprovedStore` tests).

### `build_command` / `args_builder` change

- `build_command` gains an explicit per-session parameter (consistent with the
  existing per-session `port` and `touch_name`):
  ```cpp
  Command build_command(const Settings& s, const std::string& stream_bin,
                        int port = -1, const std::string& touch_name = "",
                        const std::string& usb_aoa_serial = "");
  ```
  When `usb_aoa_serial` is non-empty: emit `--usb-aoa <serial>` and **omit**
  `--tls`/`--cert`/`--key` and the wake/listen path (the streamer already skips
  listen/TLS internally in `--usb-aoa` mode). All evdi/touch/audio/overlay/
  orientation flags are emitted as usual (the monitor is still evdi, just
  delivered over the cable). A per-session serial (not a `Settings` field) is
  used because each concurrent monitor targets a different tablet.
- Empty `usb_aoa_serial` reproduces today's behavior exactly (all existing
  `build_command` tests still pass unchanged).

### `main_window` wiring

- New members: `AoaScanner aoaScanner_;` `QList<AoaClient> aoaClients_;`
  `AoaKnownStore knownAoa_;`.
- Construct/`start()` the scanner while idle (same lifecycle as the other
  scanners); connect `clientsChanged` → store `aoaClients_` →
  `rebuildClientList()` + `evaluateAutoConnect()`.
- `rebuildClientList()`: after the tether/net rows, add one row per
  `aoaClients_` entry not already owning a session: text `"<product> — USB"`
  (append a short serial if the product name collides), `Qt::UserRole =
  "usb-aoa"`, `+1 = serial`, `+3 = serial` (used as the session key + known-store
  key). Suppress a device that already has a live session.
- `onConnectToSelectedDevice()`: branch on `transport == "usb-aoa"` → call
  `connectDevice(key="usb-aoa:"+serial, label, "usb-aoa", ident=serial,
  wakePort=0, id=serial, quietIfBusy=false)` with a **no-op** `directTablet`.
- `startSession()`: for `usb-aoa`, `build_command` gets the serial; the "Active
  monitors" row shows `"<product> · usb-aoa · :port"`. On the session's first
  connected/CONFIG signal, `knownAoa_.add(serial)`.
- `evaluateAutoConnect()`: extend to include USB-AOA candidates. A USB-AOA
  device is an auto-connect candidate iff the auto-connect toggle is on **and**
  `knownAoa_.contains(serial)`. (Network candidates keep their approved-id
  rule.) De-dupe against active sessions by key/serial as today.

## Data flow

1. Idle GUI: `AoaScanner` polls sysfs every 2 s → `aoaClients_` → list shows
   "<product> — USB" rows.
2. Manual Connect (first time) or auto-start (known serial + toggle) →
   `connectDevice` → `startSession` → `pkexec droppix_stream --usb-aoa <serial>
   …evdi flags…`.
3. Streamer switches the device to accessory mode → the tablet's
   `StreamActivity` auto-launches → streams; GUI status amber→green on video.
4. First CONFIG → `knownAoa_.add(serial)` → next plug of this tablet auto-starts.
5. Unplug → channel closes → session ends → row removed; serial stays known.

## Error handling

| Situation | Behavior |
|---|---|
| Device listed but app not installed / not droppix | Streamer switches to accessory, nothing answers; monitor row stays "waiting". After ~10 s the status hints "No droppix app responded — is it installed on the tablet?". User Stops it (existing per-session Stop). No auto-kill. |
| Device unplugged mid-session | `AoaChannel::close()` → session ends → row removed; serial remains in known-store. |
| Device stuck in accessory mode (crashed prior run) | The pure parser still *detects* accessory mode, but the GUI list **hides** accessory-mode rows (a live AOA session re-enumerates the tablet into accessory mode, so a visible "0000 — USB" row would duplicate the running session). Recover a genuinely stuck device by replugging; `aoa_connect` also resets such a device before re-handshaking. |
| Two identical models plugged | Disambiguated by serial in the label and the session key. |
| sysfs file missing/unreadable | That field is skipped; a device missing a serial is not listed (can't target `--usb-aoa`). |

## Testing

**Pure unit tests (`host/tests/test_aoa_scan.cpp`) — primary:**
- `is_android_vendor`: known vendors true, random vendor false.
- `parse_usb_sysfs` against a fake sysfs tree (temp dir):
  - allowlisted phone → listed with serial+product;
  - non-Android vendor → excluded;
  - hub (`bDeviceClass=09`) / HID / mass-storage → excluded;
  - accessory-mode device (`18d1:2d01`) → listed, `accessory_mode=true`;
  - multiple devices → all returned;
  - device with no `serial` file → excluded.

**`build_command` test (`host/gui/tests`):** USB-AOA settings → args contain
`--usb-aoa <serial>`, contain the evdi flags, and **omit** `--tls` and any wake;
non-AOA settings unchanged (existing tests still pass).

**`aoa_known_store` test:** add/contains/persist round-trip (temp config dir).

**On-device manual validation (documented, run by the user):**
1. Plug the Nexus → appears as "Nexus 10 — USB" → Connect → real evdi monitor
   over the cable.
2. Stop, unplug, replug → **auto-starts** (serial now known + toggle on).
3. A charge-only Android without the app → row appears, Connect → "waiting" +
   hint; Stop works; no other USB device is disturbed.

## File structure summary

| File | Responsibility |
|---|---|
| `host/src/aoa_scan.{h,cpp}` (new) | Pure sysfs enumeration + allowlist → `AoaDevice` list |
| `host/gui/aoa_scanner.{h,cpp}` (new) | 2 s `QObject` poller → `clientsChanged` |
| `host/gui/aoa_known_store.{h,cpp}` (new) | Serial set for auto-start gating |
| `host/gui/args_builder.{h,cpp}` (mod) | `--usb-aoa <serial>` in `build_command` |
| `host/gui/main_window.{h,cpp}` (mod) | Scanner wiring, list rows, connect branch, auto-start |
| `host/tests/test_aoa_scan.cpp` (new) | Pure parser tests |
| `host/gui/tests/…` (mod/new) | build_command + known-store tests |
| `host/CMakeLists.txt` (mod) | Build/link the new sources + tests |
