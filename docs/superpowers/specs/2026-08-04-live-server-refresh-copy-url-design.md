# Live server refresh on settings change + prominent Copy URL

**Date:** 2026-08-04
**Status:** Shipped on master (2026-08-05).
**Roadmap:** Host-GUI usability follow-up to the GUI redesign (`2026-08-03-host-gui-redesign-design.md`).

## Summary

Two host-GUI improvements:

1. **Live server refresh** — when the **Server is ON** and any *streamer-affecting* setting changes (a Settings-page field or an Interfaces toggle), the host **restarts the server listener session** so the change takes effect immediately. A connected tablet briefly drops and auto-reconnects (~1s) — the chosen "always apply immediately" behavior. Rapid edits are debounced into a single restart.
2. **Prominent Copy URL** — surface the full web-client URL (`https://ip:port`) with a **Copy URL** button whenever the server is on and the web client is enabled (not only when a tablet is connected), on both the **Interfaces** and **Status** sections.

Host GUI only (`host/gui/`). No protocol, streamer, wire, or web-serving change.

## Decisions

| Question | Decision |
| --- | --- |
| Restart timing | **Always apply immediately** — restart the server session as soon as a relevant change settles, even mid-stream (tablet auto-reconnects). |
| What triggers it | **Any streamer setting** — every launch-time Settings field (source, resolution, fps, bitrate, port, refresh, touch, audio, orientation, web-client) **and** the Interfaces toggles (LAN, USB, per-adapter). **Not** app-only prefs (theme, launch-at-login, minimize-to-tray) or the remember-auth / manage-devices buttons. |
| Copy target | The **full URL** `https://ip:port` (what pastes into a browser), surfaced prominently — not a bare IP. |
| Restart scope | The **Server listener session** (`serverKey_`) — the session that serves the web client and takes the primary connection. Manually-connected per-device monitor sessions are **not** auto-restarted (follow-up). |
| Debounce | ~600 ms single-shot, coalescing rapid edits (slider drags, several toggles) into one restart. |

## Architecture

**`SettingsPage` (`host/gui/pages/settings_page.{h,cpp}`)**
- Add a `void settingsChanged();` signal, emitted whenever a **streamer** control changes (source radios, resolution combo, fps combo, bitrate spin, port spin, refresh combo, touch check, audio check, orientation combo, web-client check). Bind it to each control's change signal (`toggled`/`valueChanged`/`currentIndexChanged`).
- **Do NOT** emit it from the app-pref controls (theme, launch-at-login, minimize-to-tray) or the remember-auth / manage-devices buttons — those don't affect the streamer.
- **`load(const Settings&)` must not emit** `settingsChanged`: wrap the control updates in `QSignalBlocker`s (or a `loading_` guard) so a programmatic profile load doesn't trigger a refresh storm. Constructor seeding likewise must not emit.

**`MainWindow` (`host/gui/main_window.{h,cpp}`)**
- New member `QTimer serverRefreshTimer_;` (single-shot, 600 ms interval); slot `void refreshServer();`.
- New `void scheduleServerRefresh();` — `serverRefreshTimer_.start()` (restarts the debounce on each call).
- Wire `connect(settingsPage_, &SettingsPage::settingsChanged, this, &MainWindow::scheduleServerRefresh);` and call `scheduleServerRefresh()` at the end of `onLanToggled`, `onUsbToggled`, and each per-adapter checkbox toggle handler (in `refreshInterfaces()`).
- `refreshServer()` (timer slot): if `serverEnabled_`, restart the listener — `stopServerSession(); startServerSession();` (the latter reads fresh `collectSettings()` → new `--web`/port/args) — then `refreshWebClientUi()` + `updateStatus()`. If the server is off, **no-op** (settings still apply on the next Start).
- Extract the web-URL computation from `refreshWebClientUi()` into `QString currentWebUrl() const` — returns the `https://ip:port` for the newest non-AOA session when `webClient && lanEnabled_ && sessions_.count() > 0`, else `""`. Both `refreshWebClientUi()` and the new Status copy button read it.

**`StatusPage` (`host/gui/pages/status_page.{h,cpp}`)**
- Add a **Copy web URL** button (hidden by default). `MainWindow` shows it (with the URL text, or just the button) when `currentWebUrl()` is non-empty, and wires its click to `QGuiApplication::clipboard()->setText(currentWebUrl())`. Updated from `refreshWebClientUi()`/`updateStatus()`.

**Interfaces web-client card** (already built): `refreshWebClientUi()` already shows `webUrlLabel_` + `webCopyBtn_` + `webQrLabel_` when `webClient && lanEnabled_ && sessions_.count() > 0`. Because the server listener is itself a session, this already appears when the server is ON (no tablet required) — confirm and keep. The copy button copies `currentWebUrl()`.

## Data flow

```
streamer setting change ──┐
Interfaces toggle ────────┼─► scheduleServerRefresh() ─► [debounce 600ms] ─► refreshServer()
                          │                                                     │
                          │                                    serverEnabled_ ? stop+start listener : no-op
                          │                                                     │
                          └────────────────────────────────────────► refreshWebClientUi()/updateStatus()
                                                                              │
                                                       currentWebUrl() ─► Interfaces card + Status Copy button
```

## Error handling / preserved behavior

- **Restart drops the connected tablet** briefly — the accepted behavior; the tablet auto-reconnects via mDNS/auto-connect (Wi-Fi) as it does after any session restart. **Web-client browsers** on the old port must reload if the port changed (documented).
- **Debounce** prevents a restart storm during slider drags / multiple quick toggles.
- **`load()` signal-blocking** prevents a profile switch (`applySettings`) from triggering a refresh.
- **Server OFF** → `refreshServer()` is a no-op; nothing restarts, settings persist in `settingsPage_` and apply on the next Start (today's behavior).
- All existing server/session/pairing/auto-connect/profile behavior is otherwise unchanged; `startServerSession()`/`stopServerSession()` are reused as-is.
- `refreshServer()` reuses `startServerSession()`'s existing failure handling (pkexec denied / port clash → `serverEnabled_ = false`), so a failed restart degrades exactly like a failed manual start.

## Testing

- **`SettingsPage` signal test** (`droppix_gui_tests`, offscreen, reuse the `ensureQApplication()` guard): constructing a `SettingsPage` and changing a **streamer** control (e.g. the bitrate spin) emits `settingsChanged`; changing an **app-pref** control (theme / launch-at-login) does **not**; and `load(Settings)` does **not** emit it (signal-blocked). This is the one piece of real, unit-testable logic.
- **Build gate:** `droppix_gui` compiles; full `droppix_tests` + `droppix_gui_tests` pass.
- **Manual (KDE):** Server ON, connect a tablet; toggle the web client in Settings → within ~0.6 s the server restarts and the tablet reconnects; the Interfaces card + Status button show the URL and **Copy URL** puts `https://ip:port` on the clipboard (pastes into a browser and loads the PWA). Toggle LAN off/on and change bitrate/port → one debounced restart each. With the Server OFF, changing settings does **not** restart anything.
  - **CAUTION (lesson):** `droppix_gui` regenerates its TLS cert every launch and a timeout-killed launch wipes it; XDG isolation does not work through distrobox — verification launches must be closed cleanly, never timeout-killed.

## Risks

| Risk | Mitigation |
| --- | --- |
| Restart storm during edits | 600 ms debounce; `load()` signal-blocked; server-off no-op. |
| Mid-stream restart annoyance | Chosen behavior; auto-reconnect; debounced to one restart. |
| `settingsChanged` wired to app-pref controls by mistake | Bind only streamer controls; the signal test asserts theme/app-prefs do **not** emit. |
| Web browser stuck on old port after a port change | Documented; user reloads the tab (rare — port changes only when the user edits it). |

## Out of scope

- Per-device (manually-connected / auto-connected) monitor auto-restart — a follow-up.
- Any protocol/streamer/wire change; the web-serving mechanism itself; QR/pairing changes.
- A bare-IP copy (the full URL is what pastes into a browser).
