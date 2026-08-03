# Host GUI redesign (spacedesk-modeled sections + status hero + dual theme)

**Date:** 2026-08-03
**Status:** Shipped on master (2026-08-03).
**Update:** Implemented in 6 tasks (Task 1 theme system + QSS restyle; Tasks 2–3 header/nav
scaffolding; Tasks 4–8 per-section `gui/pages/*` extraction — Connections, Interfaces,
Settings retiring the dialog, About; Task 9 — this one — turned page 0 into the real
Status hero: server ON/OFF **switch** (not a checkbox toggle) replacing Start/Stop,
live monitors/clients/interfaces metrics, and self-managed empty states in
`ConnectionsPage` (a muted `QLabel`, not a placeholder row, so `QListWidget::count()`
stays the real tally `MainWindow::updateStatus()` depends on). 261/261 host tests green.
**Roadmap:** UI overhaul — **host first**; the web PWA client is a separate follow-up spec.
**Mockup:** approved interactive preview — artifact `77b2eea9-e75c-462a-8491-bfe2ba032b44` (5 sections + Status hero + collapsible debug log, dark/light toggle).

## Summary

Reorganize the host GUI (`host/gui/`) from today's single dense scrolling window into a **spacedesk-style sectioned app**: a top navigation row switches a `QStackedWidget` between **Status · Connections · Interfaces · Settings · About**, with a **Status "hero" landing** (a prominent server ON/OFF plus live metrics). Restyle toward spacedesk's flat, roomy look; **add a light theme** beside the existing dark one with a **persisted in-app toggle**. Surface settings that currently have no UI. This is a **view + theming reorganization over the existing controllers** — no change to the wire protocol, the streamer, discovery/session/transport backends, encoders, evdi, or packaging.

## Reference & visual direction

Modeled on the **spacedesk Driver Console**:

- A **top row of flat section buttons** instead of one long window.
- A **server ON/OFF status hero** as the most prominent element on the landing screen.
- **"Communication Interfaces" as a first-class section** listing each network adapter's **IP address** with per-interface toggles.
- Flat, spacious layout; **teal accent** (droppix's existing `#14b8a6`, already ≈ spacedesk teal); green/amber/red for state.

droppix already *has* all of this content — it is just crammed into one window. The redesign reorganizes and reskins it; it does not add product features.

## Decisions

| Question | Decision |
| --- | --- |
| Sequence | Host GUI first. Web client is a later, separate spec. |
| Depth | Restructure **and** polish (not paint-only). |
| Reference | spacedesk Driver Console. |
| Theme | Ship **both** dark (current identity) and light (closer to spacedesk); toggle in Settings, **persisted**, applied at startup. |
| Navigation | `QStackedWidget` + a checkable, exclusive nav-button row (5 sections) + a collapsible Debug-log panel. |
| Settings surface | The gear **dialog is retired**; its options move inline into the Settings section, **plus** the currently-hidden fields (resolution, fps, audio, orientation). |

## Scope / non-goals

**In scope:** `host/gui/` view layer; `host/gui/style.h` theming (dual palette); surfacing hidden `Settings` fields; splitting the oversized `main_window.cpp`.

**Out of scope (explicit):**
- The **web PWA client** redesign — its own spec next.
- Any change to `host/src/protocol.*` / the wire, the **streamer**/stream path, discovery (`mdns_*`, `tether_scanner`, `aoa_scanner`), `SessionManager`/`StreamController`, encoders, evdi, TLS/pairing logic, or packaging.
- **No new product features.** Wall-grid, mirror/extend, stylus, etc. already exist; only their *presentation* changes.
- No change to tray / polkit pre-auth / auto-connect / `--minimized` behavior beyond where those controls are rendered.

## Architecture

Central widget becomes a vertical composition:

1. **Header bar** — logo + `droppix` wordmark + a small **theme toggle**. (The gear and About buttons are removed; they become nav tabs.)
2. **Section nav** — a horizontal row of **checkable `QPushButton`s** in an exclusive `QButtonGroup` (`objectName="navButton"`), flat-styled; the active button gets a teal tint. Index-mapped 1:1 to the stack.
3. **`QStackedWidget`** with the five page widgets.
4. **Collapsible "Debug log"** panel below the stack (reuse the existing `LogPanel`/`LogModel`), shown/hidden by a disclosure control.

**This is a view reorganization.** All existing controllers and their signal/slot wiring are preserved (`StreamController`, `SessionManager`, `MdnsAdvertiser`/`MdnsBrowser`, `TetherScanner`, `AoaScanner`, `ApprovedStore`, `CertManager`, `AutoConnect`, `autostart`, `ProfileStore`, `LogBuffer`). Widgets are **relocated** into pages; their slots and signals are unchanged.

**File split (isolation + `main_window.cpp` is already ~1218 lines):** extract each page into its own widget class under `host/gui/pages/`:

- `StatusPage`, `ConnectionsPage`, `InterfacesPage`, `SettingsPage`, `AboutPage` — each a `QWidget` that owns its child widgets and exposes the accessors/signals `MainWindow` needs. `MainWindow` shrinks to composition + orchestration (it still owns the controllers and wires them to the pages).

*Rejected alternative:* building pages as private methods inside `main_window.cpp`. It keeps everything in one 1500+-line translation unit and works against the isolation goal; dedicated page classes were chosen instead.

## Sections (content mapping to existing widgets)

**Status (landing)**
- **Server hero:** a single ON/OFF control driving `onServerToggled(bool)` (replaces the two `serverStartBtn_`/`serverStopBtn_` buttons); a state beacon reusing the existing status-color logic (`setStatusDot`); a sub-line (port + uptime); live **metrics** — active monitors, connected clients, interfaces up.
- **Profile selector** (`profileBox_` + save / save-as / delete) relocated here.
- **Scan-to-pair card:** proactive PIN + QR (`pairingScanCaption_`, `pairingScanQr_`) shown while a session waits. The transient on-connect pairing popup (`pairingPopup_`) stays as-is.

**Connections**
- **Discovered devices** list (`devicesList_`) + **Connect** (`connectBtn_` → `onConnectToSelectedDevice`).
- **Active monitors** list (`monitorsList_`) + **Stop** (`stopSelectedMonitor`) / **Switch Mirror↔Extend** (`toggleSelectedMonitorMirror`).

**Interfaces** (spacedesk "Communication Interfaces")
- **LAN card:** `lanToggle_` + per-adapter rows (`adapterRows_`), each showing adapter name + **IP**.
- **USB card:** `usbToggle_` + attached-device summary.
- **Web client card:** URL (`webUrlLabel_`) + **Copy** (`webCopyBtn_`) + QR (`webQrLabel_`).

**Settings** (retires `SettingsDialog`; a grouped form)
- **Stream:** source (test/evdi), **resolution (new UI)**, **fps (new UI)**, bitrate/quality, port, refresh.
- **Session & input:** touch, **audio (new UI)**, **orientation / auto-rotate (new UI)**, performance overlay, auto-connect, web client.
- **Application:** **theme (new: dark/light)**, launch at login, minimize to tray, remember authentication, manage remembered devices.
- The `SettingsDialog::load()/store()` round-trip over the `Settings` struct moves into `SettingsPage` unchanged in meaning; the newly-surfaced fields (`width`/`height`, `fps`, `audio`, `orientation` — unset today per `settings_dialog.cpp:128`) get real widgets.

**About**
- App identity, version, protocol/backend/encoder facts, project/issue links, and a short "getting started" (reuse `showAbout()` content).

## Theme system

- `style.h` gains a **`Theme` enum** and **`styleSheet(Theme)`** returning the QSS for a palette. Express the palette as a small **token table** (bg / surface / panel / border / text / muted / accent / good / warn / bad / idle); both themes render from one QSS template so they cannot drift structurally.
- **Dark** = today's palette. **Light** = new: near-white grounds, same teal accent, legible green/amber/red state colors on a light background.
- **Persist** the choice (`theme=dark|light`) via a config marker read at startup — mirror the existing `minimize_on_close` marker / `ProfileStore` pattern. `MainWindow` applies `qApp->setStyleSheet(styleSheet(theme))` at startup and live on toggle.
- Light-theme correctness is a first-class requirement: the status beacon, nav-active tint, pills, and buttons must all be checked on the light ground, not naively inverted.

## Error handling / preserved behavior

- All existing safety behavior is unchanged: polkit pre-auth, cert management, `--minimized` start-to-tray, tray Show/Quit, auto-connect, on-connect pairing popup, mirror/extend restart. Only the host widgets move.
- **Empty/edge states are designed**, not left blank: no devices → friendly placeholder in Connections; server off → hero shows OFF with metrics zeroed; no active monitors → Connections placeholder inviting a connection.

## Testing

- The reorg is mostly view code. **Keep all existing `droppix_gui` / `droppix_tests` green** — no behavioral regressions.
- **Extract the testable slivers** and cover them (same spirit as `autostart` / `args_builder`):
  - `styleSheet(Theme)` + theme persistence: a pure read/write helper with a `droppix_tests` case asserting dark≠light QSS and a persisted round-trip.
  - Status metric formatting (monitors/clients/interfaces summary) as a pure helper with a test.
- **Build gate:** `droppix_gui` compiles; full `droppix_tests` pass.
- **Manual on-device (KDE):** launch, click through all five sections, toggle theme and confirm it **persists across restart**, start/stop the server from the hero, connect a device, verify Interfaces IPs, confirm the Settings round-trip (including the newly-surfaced fields).
  - **CAUTION (lesson):** `droppix_gui` regenerates its TLS cert every launch, and a **timeout-killed launch wipes the real cert**; XDG isolation does **not** work through distrobox. Verification launches must be **closed cleanly, never timeout-killed**.

## Risks

| Risk | Mitigation |
| --- | --- |
| `main_window.cpp` already large | Extract per-section page classes; `MainWindow` becomes composition + wiring. |
| Regressions from relocating widgets | Preserve every signal/slot; migrate **page-by-page**, keeping the build green between pages. |
| Light-theme contrast | Per-theme tokens; explicit manual contrast check on the light ground. |
| Cert-wipe during smoke tests | Never timeout-kill a verification launch (see Testing). |

## Out of scope (reiterated)

Web client redesign (next spec), wire protocol, streamer/stream path, discovery/session/transport backends, encoders, evdi, packaging, and any new product feature.
