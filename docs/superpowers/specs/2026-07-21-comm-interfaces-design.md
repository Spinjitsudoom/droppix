# Communication Interfaces panel — Design

**Date:** 2026-07-21
**Status:** Approved (design). Not yet implemented.
**Goal:** Add a spacedesk-style "Communication interfaces" panel to `droppix_gui`: list **all** reachable LAN adapters (IP + name) with per-adapter include checkboxes so the shown URL/QR points at the *right* IP, plus **LAN** and **USB** on/off toggles that stop the relevant discovery scanners/advertising (modest resource + surface reduction). State is persisted and restored on launch.

## Summary

Today the GUI derives a single address from `primary_lan_ipv4()` — the **first** up/running interface — so on hosts with VMware/VPN/Docker adapters it can show a URL/QR for an unreachable IP (the exact footgun the spacedesk console displays: real Wi-Fi + two VMnet IPs). And the three discovery scanners (mDNS browse, USB tether, AOA) run unconditionally from launch, AOA polling USB every 2 s.

This feature: (1) enumerates **all** up IPv4 adapters and shows each with an include checkbox; the checked set drives the displayed URL/QR and "primary" IP. (2) Adds **LAN** and **USB** toggles that start/stop the transport's scanners + advertising. OFF stops discovery and refuses new auto-connects on that transport but leaves any live session streaming.

## Decisions (locked)

| Question | Decision |
| --- | --- |
| Granularity | LAN toggle + USB toggle, and per-adapter include checkboxes under LAN (IP + name + copy-URL). |
| OFF behavior | Stop that transport's scanner(s) + advertising, skip its auto-connect; **live sessions keep running**. |
| Per-adapter checkbox effect | Controls the **displayed** address set (URL/QR + primary IP). Does **not** change avahi host advertising (out of scope) or socket binding (stays `INADDR_ANY`). |
| Persistence | `configDir()` markers: `lan_disabled`, `usb_disabled` (absent = enabled), and `advertise_excluded_adapters` (newline-separated adapter names). |
| Restore | Load prefs before starting scanners in the ctor; start each scanner only if its transport is enabled. |
| Resource expectation | Real but modest: stops the 2 s USB poll + mDNS subprocess churn when off. Main wins are correct address + less surface. |

## Architecture

### New unit (pure enumeration + filter, unit-tested)

`host/gui/lan_ifaces.{h,cpp}` — no qrcodegen / web-root deps, so it links into the test target cheaply:

```cpp
namespace droppix {
struct LanIface { QString ip; QString name; };

// All up + running, non-loopback IPv4 interfaces (system query; not unit-tested).
QList<LanIface> lan_ipv4_ifaces();

// Pure filter: keep ifaces whose name is NOT in excludedNames, order preserved.
QList<LanIface> included_ifaces(const QList<LanIface>& all, const QSet<QString>& excludedNames);
}
```

`web_url.cpp` uses `lan_ifaces` for enumeration; `primary_lan_ipv4()` stays for back-compat, and a new `session_web_url(const QString& ip, int port)` builds a URL for a chosen IP.

### MainWindow changes

New state:
- `bool lanEnabled_ = true;`, `bool usbEnabled_ = true;`
- `QSet<QString> excludedAdapters_;` — adapter names the user unchecked.
- Widgets: `QGroupBox* commBox_`, `QCheckBox* lanToggle_`, `QCheckBox* usbToggle_`, and a `QVBoxLayout* adapterRows_` container rebuilt by `refreshInterfaces()`.

New methods:
- `void onLanToggled(bool on)` — set `lanEnabled_`, write/remove `lan_disabled` marker; on: `browser_.start()` (if available) + `refreshAdvertising()`; off: `browser_.stop()` + `advertiser_.stop()`. Then `refreshWebClientUi()` + `refreshInterfaces()`.
- `void onUsbToggled(bool on)` — set `usbEnabled_`, write/remove `usb_disabled` marker; on: `tetherScanner_.start()` + `aoaScanner_.start()`; off: `tetherScanner_.stop()` + `aoaScanner_.stop()`, clear `tetherClients_`/`aoaClients_`, `rebuildClientList()`.
- `void refreshInterfaces()` — rebuild adapter rows from `lan_ipv4_ifaces()`: each row is a checkbox (checked unless the name is in `excludedAdapters_`) + `"<ip> · <name>"` + a copy-URL button; toggling a checkbox updates `excludedAdapters_`, saves prefs, and calls `refreshWebClientUi()`.
- `void loadInterfacePrefs()` / `void saveInterfacePrefs()` — markers + excluded list in `configDir()`.

Wiring:
- **Ctor:** `loadInterfacePrefs()`; build `commBox_` and insert it into the main layout (above the Active-monitors / devices groups); set toggle checkboxes from state; start scanners **conditionally**:
  `if (lanEnabled_ && browser_.available()) browser_.start();` and `if (usbEnabled_) { tetherScanner_.start(); aoaScanner_.start(); }`.
- **`refreshAdvertising()`** (existing): early-return when `!lanEnabled_` (don't publish); keep advertising when on.
- **`refreshWebClientUi()`** (existing): when `!lanEnabled_`, hide the web URL/QR; else compute the primary IP as `included_ifaces(lan_ipv4_ifaces(), excludedAdapters_).value(0).ip` (fallback `127.0.0.1`) and build the URL/QR from it via `session_web_url(ip, port)`.
- **Guards** (belt-and-suspenders; scanners are already stopped when off): `onDevicesChanged` returns early if `!lanEnabled_`; `onTetherClientsChanged`/`onAoaClientsChanged` return early if `!usbEnabled_`.

## Data flow

```
launch ─► loadInterfacePrefs() ─► start scanners only for enabled transports
LAN toggle off ─► stop browser_ + advertiser_ ; hide URL/QR ; skip net auto-connect
USB toggle off ─► stop tether + AOA scanners (no more 2s USB poll) ; skip USB auto-connect
uncheck adapter ─► excludedAdapters_ += name ─► save ─► refreshWebClientUi() shows next included IP
```

## Error handling / edge cases

- No included adapters (all unchecked or none up): primary falls back to `127.0.0.1`; URL/QR still renders (local-only).
- Adapter set changes at runtime (plug/unplug ethernet): `refreshInterfaces()` re-queries on ctor, on LAN toggle, and whenever `refreshWebClientUi()` runs (session start/stop); a stale row is harmless.
- Toggling a transport off with a live session on it: session keeps streaming (OFF only gates discovery/new connections).
- `browser_.available()` false (no avahi): LAN toggle still governs advertising; browse simply never starts.

## Testing

- `host/gui/tests/test_interface_filter.cpp` (in `droppix_gui_tests`), covering `included_ifaces`:
  - empty excluded → returns all, order preserved.
  - exclude the VMnet names → only the real adapter remains.
  - exclude everything → empty list.
  - excluding a name not present → unchanged.
- Manual: on a host with VMware adapters, confirm all IPs listed; unchecking VMnet makes the URL/QR show the Wi-Fi IP; toggle USB off → AOA/tether polling stops (verify via the F12 console / no more 2 s poll lines); toggle LAN off → advertising stops and URL/QR hides; relaunch → toggles + checkboxes restored.

## Files

New:
- `host/gui/lan_ifaces.h`, `host/gui/lan_ifaces.cpp`
- `host/gui/tests/test_interface_filter.cpp`

Changed:
- `host/gui/web_url.h`, `host/gui/web_url.cpp` — use `lan_ifaces`; add `session_web_url(ip, port)`.
- `host/gui/main_window.h`, `host/gui/main_window.cpp` — panel, toggles, gating, persistence.
- `host/CMakeLists.txt` — add `gui/lan_ifaces.cpp` to `droppix_gui`; add the test + `gui/lan_ifaces.cpp` to `droppix_gui_tests`.

## Non-goals

- Filtering what avahi advertises to the network (host A-records; client-side resolution) — possible follow-up if the tablet's own discovery picks a bad IP.
- Binding listen sockets per interface (stays `INADDR_ANY`).
- iOS USB (not applicable on Linux).
- Splitting USB into separate adb / tether / AOA toggles (one USB toggle covers all).
