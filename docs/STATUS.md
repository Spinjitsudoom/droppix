# droppix — feature & docs status

**Last verified:** 2026-07-18 against `master` (`c1ef3cc`).

Living source of truth for "is this designed / planned / shipped?". Design specs under `superpowers/specs/` keep their historical detail; this file overrides stale **Status** lines until those headers catch up.

## Product maturity

| Area | State |
|---|---|
| Core extend path (evdi → H.264 → Android MediaCodec) | **Shipped** |
| Host GUI (Qt6 multi-session control panel) | **Shipped** |
| Android client (minSdk 21) | **Shipped** |
| Linux desktop receive client (`client/`) | **Shipped** |
| Packaging (AppImage + Flatpak host/client, APK script) | **Shipped** |
| macOS host backend | **Archived** (`macos/`; not in build) |
| Cross-desktop beyond KWin | **Partial** — M1 seam + X11 backend shipped; Sway/GNOME Wayland still open |

## Feature matrix (code on master)

| Feature | Status | Primary code |
|---|---|---|
| Extended virtual display (evdi) | Shipped | `host/src/evdi_frame_source.*`, `virtual_display.*` |
| H.264 encode Autocascade (NVENC → VAAPI → x264) | Shipped | `host/src/{encoder_factory,nvenc_encoder,vaapi_encoder,software_encoder}.*` |
| Wire protocol v5 | Shipped | `host/src/protocol.*`, `android/.../protocol/Protocol.kt` |
| Touch + multi-touch + 2-finger right-click | Shipped | `input_injector.*`, `mt_slots.*`, `tap_gesture.*` |
| Stylus (pressure + eraser) | Shipped | `MsgType::Pen`, `map_pen`, Android pen path |
| Keyboard + on-screen keyboard | Shipped | `MsgType::Key`, Android `KeyMap` / soft keyboard |
| Mouse scroll / buttons | Shipped | `MsgType::Scroll`, `MouseButton` |
| Auto-orientation | Shipped | `orientation.h`, `MsgType::Orientation` |
| Mirror / extend layout toggle | Shipped | `LayoutMode`, `DesktopBackend::apply_layout` |
| Audio to tablet (PipeWire) | Shipped | `audio_streamer.*`, `audio_sink.*` |
| WiFi discovery + TLS PIN pairing | Shipped | `mdns_*`, `cert_manager.*`, Android `TlsTrust` |
| USB `adb reverse` | Shipped | host GUI / streamer USB path |
| USB tethering transport | Shipped | `tether_discovery.*`, `TetherProbe.kt` |
| AOA USB accessory transport | Shipped | `aoa_{channel,connect,scan}.*`, Android `UsbAccessory` |
| Multi-monitor (N tablets) | Shipped | `session_manager.*`, `port_alloc.*` |
| Auto-connect known monitors | Shipped | `auto_connect.*` |
| Client-owned display settings (HELLO v4/v5) | Shipped | Android `AppSettings`, `client_settings.*` |
| Quality / rotation lock / stats overlay | Shipped | Android settings + `MsgType::Overlay` |
| Brightness / contrast (client-side) | Shipped | `GlDisplayView`, client `adjust_luma` |
| Render-stage horizontal flip | Shipped | Android + desktop client flip |
| DesktopBackend (KWin / X11 / Generic) | Shipped (M1+) | `desktop_backend.*` |
| Sway / GNOME Wayland backends | Roadmap | see cross-desktop spec |
| Zero-copy GPU capture | Out of scope (for now) | evdi still delivers CPU BGRA frames |

## Spec index (status as of 2026-07-18)

| Spec | Verdict |
|---|---|
| `2026-06-23-android-extended-display-design.md` | Shipped |
| `2026-06-23-droppix-wire-protocol.md` | Shipped (superseded in detail by `protocol.h` v5; see [WIRE.md](WIRE.md)) |
| `2026-06-23-phase0-spike-findings.md` | Findings (historical) |
| `2026-06-23-phase1b-device-findings.md` | Findings (historical) |
| `2026-06-24-configurable-resolution-refresh-design.md` | Shipped |
| `2026-06-24-host-gui-design.md` | Shipped |
| `2026-06-24-latency-baseline-findings.md` | Findings (historical) |
| `2026-06-24-touch-input-design.md` | Shipped |
| `2026-06-27-auto-orientation-design.md` | Shipped |
| `2026-06-27-gui-restyle-design.md` | Shipped |
| `2026-06-27-wifi-discovery-client-gui-design.md` | Shipped |
| `2026-06-28-tls-pin-pairing-design.md` | Shipped |
| `2026-06-29-audio-output-design.md` | Shipped |
| `2026-06-30-client-discovery-usb-design.md` | Shipped |
| `2026-07-02-fat-appimage-design.md` | Shipped |
| `2026-07-02-flatpak-design.md` | Shipped |
| `2026-07-02-multi-touch-design.md` | Shipped |
| `2026-07-02-pairing-code-ux-design.md` | Shipped |
| `2026-07-03-multi-monitor-design.md` | Shipped |
| `2026-07-03-two-finger-rightclick-design.md` | Shipped |
| `2026-07-04-auto-connect-known-monitors-design.md` | Shipped |
| `2026-07-05-aoa-usb-transport-design.md` | Shipped |
| `2026-07-05-cross-desktop-portability-design.md` | Partial — M1 + X11 done; M2/M3 open |
| `2026-07-05-usb-tethering-transport-design.md` | Shipped |
| `2026-07-06-aoa-m3-gui-usb-detection-design.md` | Shipped |
| `2026-07-06-desktop-backend-m1-design.md` | Shipped |
| `2026-07-10-client-owned-display-settings-design.md` | Shipped |
| `2026-07-11-quality-rotationlock-overlay-design.md` | Shipped |
| `2026-07-12-brightness-contrast-design.md` | Shipped |
| `2026-07-12-mouse-input-design.md` | Shipped |
| `2026-07-12-render-stage-flip-design.md` | Shipped |
| `2026-07-13-keyboard-input-design.md` | Shipped |
| `2026-07-13-mirror-mode-design.md` | Shipped |
| `2026-07-13-onscreen-keyboard-design.md` | Shipped |
| `2026-07-13-stylus-design.md` | Shipped |
| `2026-07-16-hw-encode-design.md` | Shipped |

## How to keep this current

When a feature lands on `master`:

1. Update the matching row here to **Shipped** (or **Partial** with what remains).
2. Set the design spec header to `**Status:** Shipped (YYYY-MM-DD).`
3. Leave the historical plan as-is (plans are build journals; do not rewrite mid-flight checklists after the fact unless fixing factual errors).
