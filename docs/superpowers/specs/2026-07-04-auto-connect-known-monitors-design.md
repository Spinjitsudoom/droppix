# Auto-Connect Known Monitors — Design

**Date:** 2026-07-04
**Status:** Shipped on master.

## Goal

When the droppix host GUI is running, it automatically connects the tablets that
belong to it — over both USB and Wi-Fi — instead of the user selecting each
device in the list and clicking **Connect** one at a time. Opening the app on a
tablet (Wi-Fi) or plugging it in (USB) should be enough to bring its monitor up.

## Chosen behavior (decisions)

- **Who initiates:** host-initiated. The host GUI drives the connection, reusing
  the existing per-session-port model (spawn a streamer, then USB `usbConnect` or
  network `WAKE`). No new "greeter/listener" architecture.
- **Which tablets:** any tablet the host already trusts, gated by a single host
  toggle **"Auto-connect known monitors" (default on)**.
  - **USB:** any plugged-in tablet with the app installed (physical connection =
    trust; localhost is already auto-approved).
  - **Wi-Fi:** any discovered tablet whose advertised device id is in the host's
    approved store **and** which has this host paired. First-ever connect for a
    tablet stays manual — that is where pairing (PIN) + host approval get set up.
- **Transports:** both USB and Wi-Fi.
- **Toggle off** = today's fully-manual behavior; existing monitors are never
  torn down by toggling.

## Trust & eligibility model

Auto-connect fires for a device only when it is **not already in a session** and
is **eligible**:

- **USB eligible:** the USB scan already lists only tablets with the app
  installed; those are eligible. Auto-connect runs the existing `usbConnect`
  (adb-reverse + `am start` launch). No pairing concept (localhost trusted).
- **Wi-Fi eligible:** the discovered tablet's advertised `id` is present in the
  host's approved store (`approved_`, which already accumulates the tablet's
  stable id on every successful connect, including the WAKE auto-approve path).
  The host WAKEs it; the existing 15 s auto-approve-on-WAKE window covers
  host→tablet approval; the tablet auto-accepts because it has the host paired.

Nothing auto-connects that both sides have not already trusted once manually.

## Wi-Fi eligibility matching (why TXT id)

At mDNS **discovery** time the host sees only the tablet's service name
(`Build.MODEL`), address, and wake port — not its stable device id — so it cannot
match a discovered tablet against its approved store. The tablet therefore
advertises its stable id in the `_droppix-client._tcp` **TXT record**
(`id=<stableId>`), and the host reads it to match precisely.

Alternatives rejected:
- *WAKE everyone, let the tablet filter* — spawns a streamer and burns a port
  (cap 4) for every unpaired tablet that will just ignore the WAKE. Wasteful.
- *Match by name* — `Build.MODEL` collides (two "Nexus 10"s) and is fragile.

The stable id is a random per-install UUID (an identifier, not a secret), so
broadcasting it on the LAN is acceptable.

## Host architecture

### `AutoConnectPolicy` (new, pure) — `host/gui/auto_connect.{h,cpp}`

A pure decision unit with no Qt / mDNS / adb dependencies, so it is unit-testable
in isolation:

```
struct AutoConnectCandidate { QString key; QString transport; bool eligible; };

// Returns the keys that should be auto-connected now.
QList<QString> devicesToConnect(bool enabled,
                                const QList<AutoConnectCandidate>& candidates,
                                const QSet<QString>& activeKeys);
```

Rule: return each candidate where `enabled && eligible && !activeKeys.contains(key)`.
All "already connected / disabled / ineligible" filtering lives here.

### `MainWindow` wiring (no new decision logic of its own)

- **Trigger:** the existing discovery signals already fire on change — USB
  `clientsChanged(...)` and the mDNS browser found/lost. On each, MainWindow
  builds the candidate list:
  - USB candidate: `eligible = true`.
  - Wi-Fi candidate: `eligible = approved_.isApproved(txtId)`.
- It passes the candidates and the current `sessions_` keys to
  `AutoConnectPolicy::devicesToConnect`, then for each returned key calls the
  **same** path manual connect uses today. `onConnectToSelectedDevice`'s body is
  factored into a shared `startAutoSession(key, label, transport, ident, wakePort)`
  helper so auto and manual connect run identical logic — port allocation, streamer
  spawn, `usbConnect`/WAKE, session tracking, and the cap all apply unchanged.
- **Debounce:** a single-shot timer (≈750 ms) coalesces discovery bursts and gives
  a just-appeared tablet a moment to have its app ready before the WAKE.
- **No storms:** a device already in `sessions_` is skipped by the policy; a
  per-key "attempt in flight" guard prevents re-firing while a connect is pending;
  a failed attempt retries on the next (debounced) discovery tick.

### Supporting host change

- Extend the host mDNS browser to parse the TXT `id` attribute and surface it on
  discovered network devices (stored on the list item alongside the existing
  `UserRole+*` transport/ident/wake-port data).

### Settings

- Add `autoConnect` (bool, default `true`) to `Settings`.
- Add an "Auto-connect known monitors" checkbox to the settings dialog.
- Persisted with the existing profile store (round-trips like the other settings).

## Tablet architecture

Two small changes in the client's connect/advertise path; no wire/protocol change
and no `StreamActivity` change.

- **Advertise the device id.** `WakeService` registers `_droppix-client._tcp`
  with name + port; add one TXT attribute `id=<DeviceIdentity.stableId>` via
  `NsdServiceInfo.setAttribute`.
- **Auto-accept a WAKE from a paired host.** Today `ConnectActivity`'s wake
  callback always shows the "Connect to X?" dialog. Branch on trust:
  - `tlsTrust.isPaired(host)` → connect immediately, no dialog.
  - not paired → keep today's confirm dialog (first-time connects stay manual).

  Factor the branch into a pure helper `shouldAutoAccept(isPaired): Boolean` so it
  is unit-testable even though the Activity is not.

## Edge cases

- **Monitor cap (4):** auto-connect goes through `allocatePort`; a −1 result skips
  the device silently (same as manual).
- **Already connected / duplicate:** filtered by the policy via `activeKeys`; a
  tablet is never double-connected.
- **Auto-reconnect for free:** a dropped session is removed from `sessions_`; when
  the tablet re-appears in discovery the policy re-selects it (covers "left Wi-Fi
  and came back" and "app relaunched").
- **Failed attempt:** the in-flight guard clears on failure/timeout; the next
  discovery tick retries. Debounced, so no tight loop.
- **Toggle off mid-session:** stops new auto-connects; does not tear down existing
  monitors.
- **Unpaired Wi-Fi tablet:** ineligible, never WAKEd, so no wasted streamer/port.

## Testing

- **Host (pure, ctest):** `AutoConnectPolicy` unit tests — toggle off → empty;
  ineligible net (id not approved) → skipped; eligible USB → included;
  already-active key → skipped; mixed list → only the correct keys. Plus a test
  for the mDNS-browser TXT `id` parsing.
- **Tablet (JVM unit test):** `shouldAutoAccept(isPaired)` — true when paired,
  false otherwise (mirrors the existing `WakeTest` style).
- **Manual end-to-end on hardware:** toggle on, open the app on the Nexus (Wi-Fi,
  paired) and the phone (USB) → both monitors come up with no per-device clicking;
  drop one → it reconnects on reappear; toggle off → no new auto-connects.

## Out of scope (YAGNI)

- No always-on host listener / port-assigning greeter (that was the rejected
  "tablet dials the host" flow).
- No remembering per-tablet monitor position/resolution.
- No separate tablet-side auto-connect opt-in toggle (the host toggle + required
  pairing already gate it).
- No teardown-on-toggle-off.
