# Client settings, round 2: Quality, Rotation lock, Performance overlay

**Date:** 2026-07-11
**Status:** Shipped on master.
**Builds on:** the client-owned display settings feature (resolution/FPS/audio) — Linux Phase 1 on `master`, Android Phase 2 in PR #1.

## Summary

Three Spacedesk-parity additions to the client settings menu (roadmap tier T1):

1. **Quality / Bitrate** — a client-chosen encode bitrate, sent to the host (like fps/audio). Host + protocol + both clients.
2. **Rotation lock** — Android can lock to its current orientation instead of following the sensor. Android-only.
3. **Performance overlay** — a client toggle for the on-tablet RTT/FPS/decode HUD. Android-only.

Rotation-lock and Overlay need no host or protocol change. Quality needs one new HELLO field (v5).

## Decisions

| Question | Decision |
| --- | --- |
| Quality scope | Host + **both** clients (Android + Linux), so both sit at HELLO v5 (no version skew). |
| Quality granularity | Three presets: **Low / Medium / High = 4000 / 8000 / 16000 kbps**. 8000 = today's host default. |
| Rotation lock scope | Android-only (the Linux client has no sensor; its manual rotation picker already covers this). |
| Overlay scope | Android-only (the Linux client has no such HUD today). |

## 1. Quality / Bitrate

### Protocol (`host/src/protocol.{h,cpp}` + `android/.../protocol/Protocol.kt`)
- Bump `kProtocolVersion` / `Protocol.VERSION` 4 → 5.
- HELLO v5 body appends **`u32 bitrate_kbps`** immediately after `orientation_code`, before the name/id strings — written only when `version >= 5`. New wire layout:
  `u32 version, u32 width, u32 height, u32 density, u32 fps, u8 audio_wanted, u8 orientation_code, u32 bitrate_kbps, u16-len name, u16-len id`.
- `decode_hello`/`encode_hello` (C++) and `encodeHello` (Kotlin) gain a trailing `bitrate` param; version-gated so v4/v3/v2 bodies are unchanged and decode `bitrate = 0` (sentinel → host default).

### Host (`host/src/session_params.{h,cpp}`, `stream_daemon.cpp`, `transport_server.{h,cpp}`)
- `read_hello` surfaces one more out-param (`bitrate`).
- `select_session_params` gains `hello_bitrate` + `default_bitrate` and returns `bitrate` (client value when `v≥5 && >0`, else the `cfg_.bitrate_kbps` fallback). Extend `SessionParams`.
- `stream_daemon` uses `sp.bitrate` in `enc_.open(w, h, sp.fps, sp.bitrate)` instead of `cfg_.bitrate_kbps`.

### Android (`AppSettings`, `SettingsActivity`, `TransportClient`, `StreamActivity`)
- `AppSettings.bitrateKbps: Int = 8000`.
- A **Quality** spinner in `SettingsActivity`: Low / Medium / High mapped to 4000 / 8000 / 16000; seed from the stored value; save it.
- Thread `bitrateKbps` through `TransportClient.run`/`runOverChannel` → `encodeHello` at both AOA + Wi-Fi call sites (same shape as fps/audio).

### Linux client (`client/src/client_settings.{h,cpp}`, `client/gui/settings_dialog.*`, `transport_client.*`, `main_window.cpp`)
- `ClientSettings.bitrate_kbps = 8000`; a **Quality** dropdown (Low/Medium/High) in `ClientSettingsDialog`.
- Thread it through `runOverChannel` → `encode_hello`. Both clients now send v5.

## 2. Rotation lock (Android-only)

- `AppSettings.rotationLocked: Boolean = false`.
- A **Rotation** spinner in `SettingsActivity`: **Auto** (default) / **Locked**.
- In `StreamActivity`:
  - **Locked:** call `requestedOrientation = SCREEN_ORIENTATION_LOCKED` (freezes the device to its current orientation) and suppress the sensor `ORIENTATION` messages (guard the `orientationListener` callback on the setting). The host keeps the orientation it was seeded with from HELLO.
  - **Auto:** today's behavior (`fullSensor` + live `ORIENTATION`).
- HELLO still carries `orientationMapper.currentCode()` at connect in both modes (preserves the no-restart-loop invariant).
- No host or protocol change.

## 3. Performance overlay (Android-only)

- `AppSettings.showOverlay: Boolean = false`.
- A **Performance overlay** switch in `SettingsActivity`.
- In `StreamActivity`: the overlay `TextView` + `overlayTick` already render RTT/fps/decode from `StatsSink`. Change visibility to be driven by the local setting (`View.VISIBLE` when on), independent of — but still OR-able with — the host's existing `OVERLAY` message. Read the setting in `startStreaming()` so a mid-stream change applies on the settings-reconnect.
- No host or protocol change.

## Settings menu after this

Resolution · **Quality** · Frame rate · **Rotation (Auto/Locked)** · Audio · **Performance overlay** (Android). The Linux client's dialog gains **Quality** (its rotation is already a manual picker; no HUD).

## Testing

- **Protocol:** `encode/decode_hello` v5 round-trip + v4/v3 back-compat sentinels (C++ `test_protocol` + Kotlin `ProtocolTest` — assert `bitrate` at the new offset for v5, and the old layout for v4).
- **Host:** `select_session_params` bitrate matrix (v5 client value vs v<5/zero fallback).
- **Clients:** `AppSettings` / `ClientSettings` persistence round-trip incl. the new fields.
- **Localhost E2E:** a v5 HELLO with a distinct bitrate → confirm the host encodes at it (log / stats); a v4 HELLO → host falls back to default (no crash).
- **On-device:** Quality Low vs High visibly changes stream sharpness/bandwidth; Rotation Locked stops the display following the tablet; overlay toggle shows/hides the HUD.

## Out of scope

- The other T1 items (Flipped display, Mouse input) — separate follow-up.
- Color adjustments (brightness/contrast, T2) — need a GL render stage, not part of this.
