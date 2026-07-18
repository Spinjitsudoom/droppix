# Pairing-code UX: connect-time popup, forget devices, per-restart code — design

**Date:** 2026-07-02
**Status:** Shipped on master.

## Goal

Make the pairing code appear exactly when it's needed (a device connecting), let the
user forget remembered devices, and rotate the code on every launch.

## Background

- **Cert / code:** `CertManager` generates a persistent self-signed cert once; the
  6-digit pairing code derives from its DER. Today it's stable across restarts and
  shown in an always-visible `pairingLabel_`.
- **TLS pairing:** the tablet does a TLS *probe* to grab the PC cert, then prompts the
  user to enter the 6-digit code; on success it pins the cert fingerprint (silent
  reconnect until the cert changes).
- **Approval:** separately, the streamer sends `approve-request id ip name` after HELLO;
  the GUI shows "Allow X?" (or auto-approves remembered/just-woken peers). `ApprovedStore`
  persists approved ids.

## Decisions (from brainstorming)

1. Pairing code shows as a **popup on connect only** (remove the always-visible label).
2. Forget devices = a **list with per-device remove** (+ Forget all).
3. **Regenerate the code every restart** (paired tablets re-pair once per session).

## Components

### 1. Per-restart code — `CertManager::regenerate()`

```cpp
bool regenerate();  // remove cert.pem/key.pem, reset the cached code, re-run ensure()
```

`MainWindow` calls `cert_.regenerate()` at startup instead of `ensure()`. Fresh cert →
fresh code each launch. `pairingCode()`'s cache (`code_`/`codeComputed_`) is reset.

### 2. Connect-time popup

- **Streamer** (`stream_daemon.cpp`): right after `accept_client()` succeeds and before
  `read_hello()`, emit `client-connecting ip=<peer_ip>` to stderr. This fires during the
  tablet's PIN probe — the moment it needs the code.
- **StreamController**: parse that line → new signal `connecting(QString ip)`.
- **MainWindow**: remove `pairingLabel_`. Add a non-modal popup (`pairingPopup_`, a
  `QDialog`) with the connecting IP + the current `cert_.pairingCode()` shown large.
  - `showPairingPopup(ip)`: skip if `ip == "127.0.0.1"` (USB needs no PIN); otherwise
    set text + code, `show()/raise()`, and (re)start a 90 s single-shot auto-hide timer.
  - `hidePairingPopup()`: hide + stop the timer.
  - Shown on `connecting(ip)`; hidden on `approvalRequested` (device paired → HELLO),
    on `runningChanged(false)`, on the timeout, or via the popup's Close button.
  - A already-paired reconnect just flashes it briefly (connect → HELLO → hide).

### 3. Forget saved devices

- `ApprovedStore`: add `QStringList ids() const;` and `void remove(const QString& id);`
  (rewrites the file from the in-memory set). `clear()` already exists.
- `SettingsDialog`: a **"Manage remembered devices…"** button → `manageDevicesRequested()`
  signal (mirrors `rememberAuthRequested`).
- `MainWindow::manageDevices()`: a modal dialog with a `QListWidget` of `approved_.ids()`
  (ids/IPs — the only info the PC stores) + **Forget selected** and **Forget all**,
  refreshing the list after each action.

## Data flow

```
tablet probes PC (TLS) ─► streamer accept_client ─► "client-connecting ip=X"
   └─► StreamController.connecting(X) ─► MainWindow.showPairingPopup(X)  [code shown]
tablet user enters code ─► pairs ─► real connect ─► HELLO ─► approve-request
   └─► MainWindow: hidePairingPopup() + "Allow X?" dialog
```

## Error handling / edges

- Cert can't be read → `pairingCode()` returns "unavailable"; popup still shows that.
- Pairing abandoned (wrong code / cancel) → no HELLO → popup auto-hides after 90 s (or Close).
- USB / localhost → no popup.
- Empty remembered list → the manage dialog shows "(none)".

## Testing

- Unit-test `ApprovedStore::ids()` and `remove()` in `tests/test_approved_store.cpp`.
- Popup + streamer line + manage dialog verified manually (GUI/Qt paths aren't unit-tested).

## Out of scope (YAGNI)

- Friendly device names in the remembered list (the PC only stores ids/IPs).
- Manual "regenerate now" button (rotation is automatic per restart).
