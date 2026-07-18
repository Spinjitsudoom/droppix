# Unified client discovery (network + USB) — design

**Date:** 2026-06-30
**Status:** Shipped on master.

## Goal

While the host GUI is idle (server stopped), continuously scan for droppix client
apps reachable over **both** transports — WiFi (mDNS) and **USB (adb)** — and show
them in one "Available clients" list. The server starts only when the user selects
a client and clicks Connect.

## Background (what already exists)

- **Network:** `MdnsBrowser` runs `avahi-browse -rptk _droppix-client._tcp` every 3 s
  from GUI launch and populates the "Devices on network" list. Connecting sends a UDP
  WAKE datagram; the tablet then dials the PC. This already runs before the server.
- **USB:** `AdbManager.refresh()` runs `adb devices` every 3 s and shows a *status
  label* ("Device: <serial> — connected"). It detects *any* Android device, not a
  droppix client, and it is not a connectable list entry.

The gap: USB clients are not unified into the discoverable list, and there is no
USB connect action.

## Decisions

1. **Behavior:** list only; server starts on Connect (matches the network flow).
2. **USB detection:** confirm the app is installed — `adb -s <serial> shell pm list
   packages com.droppix.app` — so only tablets that actually have droppix appear.
3. **USB connect:** the host launches the app on the tablet via adb (no tap needed);
   localhost is pairing-exempt, so no PIN.

## Components

### `host/src/usb_scan.{h,cpp}` (pure, unit-tested)

```cpp
struct AdbDevice { std::string serial; std::string state; };  // state: device/unauthorized/offline/...
std::vector<AdbDevice> parse_adb_devices(const std::string& text);   // parses `adb devices`
bool adb_package_present(const std::string& pm_output, const std::string& pkg);  // `pm list packages` contains package:<pkg>
```

No I/O — mirrors `parse_avahi_browse`. The `adb devices` parser skips the
"List of devices attached" header and blank lines, and splits `serial\tstate`.

### `host/gui/usb_client_scanner.{h,cpp}` (QObject, 3 s QTimer — mirrors `MdnsBrowser`)

```cpp
struct UsbClient { QString serial; QString model; };
// signals:
//   void clientsChanged(QList<UsbClient>);
//   void statusChanged(QString);   // "" when happy; else e.g. "USB device unauthorized — accept the prompt on the tablet"
// methods: bool available(); void start(); void stop();
```

Orchestration (chained async `QProcess`, not unit-tested, like `MdnsBrowser`):
`adb devices` → for each `device`-state serial: `adb -s <serial> shell pm list
packages com.droppix.app`; if present, `adb -s <serial> shell getprop
ro.product.model` for a label → emit the collected `UsbClient`s. If a device is in
`unauthorized` state (or adb is missing), emit a `statusChanged` hint and no client.

### `host/gui/adb_manager.{h,cpp}` (extend)

Add `void usbConnect(const QString& serial, int port);` — runs
`adb -s <serial> reverse tcp:<port> tcp:<port>` then
`adb -s <serial> shell am start -n com.droppix.app/.ui.ConnectActivity
--ez usb_autoconnect true --ei usb_port <port>`. Keep `setupReverse(port)` for the
existing auto-adb path.

### `host/gui/main_window.{h,cpp}` (unified list + connect branch)

- Rename the group box "Devices on network" → **"Available clients"**.
- Hold both sources: `QList<MdnsDevice> netDevices_`, `QList<UsbClient> usbClients_`.
  Each source signal stores its list and calls `rebuildClientList()`, which clears
  `devicesList_`, adds USB entries then network entries, and restores the prior
  selection. Each `QListWidgetItem` stores a **transport tag** (UserRole) plus its
  payload: USB → serial; Network → address + wake port.
  Labels: `Nexus 10 — USB`, `Nexus 10 — 192.168.1.40`.
- `deviceLabel_` shows the adb-status hint (`UsbClientScanner::statusChanged`):
  unauthorized / adb-not-found. Confirmed clients live in the list.
- `onConnectToSelectedDevice()` branches on the transport tag:
  - **Network:** unchanged — start server if needed, then send WAKE UDP.
  - **USB:** start server if needed, then `adb_.usbConnect(serial, port)`.

### Android `ConnectActivity.kt` (one small change)

At the end of `onCreate`, after wiring the USB button:

```kotlin
if (intent.getBooleanExtra("usb_autoconnect", false)) {
    status.text = "Connecting over USB..."
    connectTo("127.0.0.1", intent.getIntExtra("usb_port", 27000))
}
```

Reuses the existing `connectTo` path (localhost → `launchStream`, pairing-exempt).
No manifest change — `ConnectActivity` is the exported launcher.

## Data flow

```
[idle GUI]
  MdnsBrowser  --(avahi-browse)-->  netDevices_  --\
                                                    >-- rebuildClientList() --> devicesList_
  UsbClientScanner --(adb devices + pm list)--> usbClients_ --/

[Connect clicked]
  network item -> startServer? + WAKE UDP -> tablet dials PC
  usb item     -> startServer? + adb reverse + am start ConnectActivity(usb_autoconnect,port)
                  -> tablet auto-connects to 127.0.0.1:<port>
```

## Error handling

- adb missing / not in PATH → `statusChanged("adb not found")`, empty USB list.
- Device `unauthorized` → hint to accept the RSA prompt; no client listed.
- `pm`/`getprop` failures → that device is simply omitted (best-effort).
- A previous scan still running when the timer fires → skip (as `MdnsBrowser` does).
- Connect with no selection → no-op (existing behavior).

## Testing

- **Host unit tests** (`tests/test_usb_scan.cpp`): `parse_adb_devices` — empty,
  header-only, one device, multiple devices, unauthorized, offline, trailing blank
  lines, CRLF; `adb_package_present` — present, absent, substring-safety.
- **Android:** `assembleDebug` + manual. The Activity-launch path is verified by hand
  (no heavyweight instrumentation test).

## Out of scope (YAGNI)

- Auto-starting the server on discovery.
- Changing the manual USB button's hardcoded `27000`.
- PC-initiated *network* connect (already exists via WAKE).
