# Architecture: droppix

**Last verified:** 2026-07-18 against local/fork `master`.

Living overview of how droppix turns a client (Android tablet or Linux receive app) into an extended (or mirrored) monitor for a Linux host. Feature ship-state: [`STATUS.md`](STATUS.md). Wire details: [`WIRE.md`](WIRE.md).

## System at a glance

One host control panel (`droppix_gui`) can run **N** streamer processes (`droppix_stream`). Each streamer owns one virtual display (evdi), one H.264 encode path, one transport session, and one set of uinput devices. Clients decode video and send input / orientation / settings back on the same connection.

```mermaid
graph TB
    subgraph Host["Linux host"]
        GUI["droppix_gui<br/>Qt6 control panel"]
        PW["PipeWire sink<br/>droppix-audio"]
        DB["DesktopBackend<br/>KWin / X11 / Generic"]

        subgraph Streamer["droppix_stream (per session, often via pkexec)"]
            EVDI["evdi virtual display"]
            CAP["FrameSource<br/>EvdiFrameSource / TestPattern"]
            ENC["Encoder<br/>AutoEncoder: NVENC then VAAPI then x264"]
            TX["TransportServer<br/>ByteChannel + TLS"]
            INJ["InputInjector<br/>touch / pen / keys / mouse"]
            AUD["AudioStreamer<br/>pw-record monitor"]
        end

        GUI -->|"spawn + stdin control"| Streamer
        GUI --> PW
        EVDI --> CAP
        CAP --> ENC
        ENC --> TX
        AUD --> TX
        TX --> INJ
        Streamer --> DB
        DB -->|"outputs / map_touch / map_pen / layout"| COMP["Compositor<br/>KWin or X11"]
        EVDI -->|"DRM connector"| COMP
    end

    subgraph Clients["Clients"]
        AND["Android app<br/>MediaCodec + GlDisplayView"]
        LNX["Linux client<br/>FFmpeg decode + Qt"]
        WEB["Web PWA<br/>WebCodecs + WSS"]
    end

    TX -->|"wire protocol v5"| AND
    AND -->|"input / HELLO"| TX
    TX -->|"wire protocol v5"| LNX
    LNX -->|"input / HELLO"| TX
    TX -->|"WSS binding"| WEB
    WEB -->|"input / HELLO"| TX
```

## Process topology

The GUI never embeds the encode loop. It builds a CLI, launches `droppix_stream` (often under `pkexec` for root uinput/evdi), and parses `--stats-json` / stdin control lines.

```mermaid
graph LR
    USER[Operator] --> GUI[droppix_gui]
    GUI -->|"QProcess + args"| S1[droppix_stream session 1]
    GUI --> S2[droppix_stream session 2]
    GUI -->|"pactl / null sink"| PW[PipeWire droppix-audio]
    S1 -->|"pw-record as user"| PW
    S1 -->|"uinput + /dev/dri"| KERNEL[Kernel: evdi + uinput]
    S2 --> KERNEL
```

| Process | Privilege | Role |
|---|---|---|
| `droppix_gui` | user | Discovery, pairing UI, session manager, audio sink create, spawn streamers |
| `droppix_stream` | often root via `pkexec` | Accept client, create evdi, encode, inject input, stream audio |
| Android / `droppix_client` / web PWA | device / user / browser | Decode, display, capture input, send HELLO settings |

## Video path

Capture is CPU BGRA from evdi today (no zero-copy GPU capture). Encode prefers GPU when available.

```mermaid
sequenceDiagram
    participant C as Client
    participant T as TransportServer
    participant D as StreamDaemon
    participant F as FrameSource
    participant E as Encoder
    participant K as Compositor + evdi

    C->>T: HELLO v5 dims fps bitrate audio
    T->>D: handshake OK plus approve gate
    D->>F: make_source then start evdi
    F->>K: EDID plus connector appears
    D->>K: DesktopBackend adopt layout map devices
    D->>E: open encoder
    D->>T: CONFIG
    loop each frame
        F->>D: Frame BGRA
        D->>E: encode H.264 AU
        E->>T: VIDEO with in-band SPS PPS on IDR
        T->>C: VIDEO
        C->>C: MediaCodec or FFmpeg display
    end
```

### Encoder cascade

```mermaid
flowchart LR
    F[BGRA frame] --> NV12[Nv12Converter]
    NV12 --> AUTO{AutoEncoder preference}
    AUTO -->|auto / nvenc| N[NvencEncoder h264_nvenc]
    AUTO -->|fallback| V[VaapiEncoder h264_vaapi]
    AUTO -->|fallback| S[SoftwareEncoder libx264]
    N --> AU[Annex-B AU]
    V --> AU
    S --> AU
```

- Factory: `host/src/encoder_factory.*` (`make_encoder`, `--encoder auto|nvenc|vaapi|software`).
- All backends keep **in-band SPS/PPS on every IDR** so clients configure from the first keyframe (`CONFIG.extradata` usually empty).

## Input / control path

Clients normalize; host replays via uinput. Multi-touch, pen, keys, scroll, and mouse buttons are separate message types (see [`WIRE.md`](WIRE.md)).

```mermaid
sequenceDiagram
    participant UI as Client UI
    participant TC as TransportClient
    participant TS as TransportServer
    participant IJ as InputInjector
    participant DB as DesktopBackend

    UI->>TC: touch / pen / key / scroll / mouse
    TC->>TS: Touch / Pen / Key / Scroll / MouseButton
    TS->>IJ: inject absolute / relative / EV_KEY
    Note over IJ: uinput devices bound to droppix output
    IJ->>DB: map_touch / map_pen (best-effort)
    DB->>DB: KWin DBus or xinput map-to-output
```

| Client event | Wire | Host device |
|---|---|---|
| Fingers | `Touch` (+ legacy `Input`) | `droppix-touch` (MT slots) |
| Stylus | `Pen` | pen uinput (`ABS_PRESSURE`, eraser) |
| Keyboard / OSK | `Key` | `droppix-keyboard` |
| Wheel / RMB/MMB | `Scroll` / `MouseButton` | aux pointer |
| Orientation | `Orientation` | may force session rebuild at new dims |
| Settings | HELLO fields | `select_session_params` |

## Multi-monitor sessions

```mermaid
graph TB
    GUI[SessionManager in droppix_gui]
    GUI --> P1[port / touch-name alloc]
    GUI --> S1[streamer A]
    GUI --> S2[streamer B]
    S1 --> E1[evdi monitor A @ native res]
    S2 --> E2[evdi monitor B @ native res]
    A1[Tablet A] -->|wire| S1
    S1 -->|video| A1
    A2[Tablet B] -->|wire| S2
    S2 -->|video| A2
```

- One `droppix_stream` per tablet; GUI owns lifecycle (`host/gui/session_manager.*`, `port_alloc.*`).
- Source factory runs **after** HELLO so the evdi EDID matches the client’s native size.

## Transports

Same length-prefixed protocol; different `ByteChannel` under `TransportServer`.

```mermaid
flowchart TB
    PROTO[Wire protocol v5]
    PROTO --> SOCK[SocketChannel TCP]
    PROTO --> AOA[AoAChannel USB accessory]
    SOCK --> WIFI[WiFi + mDNS + TLS PIN]
    SOCK --> ADB[USB adb reverse to localhost]
    SOCK --> TETHER[USB tethering + UDP probe]
    AOA --> ACCESSORY[libusb host to UsbAccessory app]
```

| Path | Discovery | Trust |
|---|---|---|
| WiFi | Avahi mDNS | TLS cert pin + 6-digit PIN / approved store |
| `adb reverse` | GUI USB path | localhost |
| USB tether | `TetherProbe` / `tether_discovery` | TLS + device id |
| AOA | `aoa_scan` / known store | accessory link |

## DesktopBackend seam

Compositor-specific work is behind one interface (`host/src/desktop_backend.*`). Display creation itself is evdi; backends handle geometry, touch/pen binding, and mirror/extend layout.

```mermaid
graph TB
    IFACE["DesktopBackend interface<br/>outputs map_touch map_pen<br/>adopt_output apply_layout"]
    IFACE --> KWIN["KWinBackend<br/>kscreen-doctor + KWin DBus"]
    IFACE --> X11["X11Backend<br/>xrandr + xinput map-to-output"]
    IFACE --> GEN["GenericBackend<br/>display may work - touch map no-op"]
```

| Backend | Shipped | Notes |
|---|---|---|
| `KWinBackend` | Yes | `kscreen-doctor` + KWin InputDevice DBus |
| `X11Backend` | Yes | `xrandr` + `xinput map-to-output` |
| `GenericBackend` | Yes | Display may work; touch map is a no-op |
| Sway / GNOME Wayland | No | See [cross-desktop roadmap](superpowers/specs/2026-07-05-cross-desktop-portability-design.md) |

Roadmap (not shipped): Sway / GNOME Wayland backends - see [`superpowers/specs/2026-07-05-cross-desktop-portability-design.md`](superpowers/specs/2026-07-05-cross-desktop-portability-design.md).

## Client decode paths

```mermaid
graph LR
    subgraph Android
        MC[MediaCodec] --> ST[SurfaceTexture OES]
        ST --> GL[GlDisplayView shader]
        GL --> FX[flip / brightness / contrast]
    end
    subgraph LinuxClient["Linux client"]
        FF[FFmpeg decode] --> QV[Qt video widget]
        QV --> FX2[flip / luma adjust]
    end
    VID[VIDEO AUs] --> MC
    VID --> FF
```

Android render stage is also the hook for client-side image adjustments; host still sends plain H.264.

## Handshake (simplified)

```mermaid
sequenceDiagram
    participant C as Client
    participant H as Host streamer

    Note over C,H: WiFi: TLS + PIN / auto-accept before HELLO
    C->>H: HELLO v5
    H->>H: approve gate (if required)
    H->>H: create FrameSource + open Encoder
    H->>C: CONFIG
    H->>C: VIDEO stream
    C->>H: Touch Pen Key messages
    H->>C: Audio Overlay optional
    C->>H: Ping
    H->>C: Pong
```

## Repository map

| Path | Responsibility |
|---|---|
| `host/src/` | Streamer core: protocol, evdi, encoders, transport, AOA, tether, input, audio, desktop_backend |
| `host/gui/` | Qt6 GUI: sessions, mDNS, TLS/PIN, auto-connect, settings, AOA/tether scanners |
| `android/` | Kotlin tablet client |
| `client/` | Qt6 Linux receive client (shares `host/src/protocol.cpp`) |
| `web/` | Host-served PWA (TypeScript → `web/dist`; served by `--web`) |
| `packaging/` | AppImage / Flatpak / APK |
| `macos/` | Archived host backend (not in build) |
| `docs/` | STATUS, WIRE, this file, specs/plans, lessons |

## Hard constraints (load-bearing)

1. Streamer needs root for uinput + evdi on the primary path (`pkexec`).
2. Kernel `evdi` module must exist on the host; it cannot ship inside an AppImage.
3. Flatpak reaches the host via `flatpak-spawn --host` (sandbox effectively escaped for integration).
4. C++ / Kotlin / desktop-client protocol codecs must stay byte-identical; bump `kProtocolVersion` on HELLO/wire shape changes and update [`WIRE.md`](WIRE.md) in the same change.

## Related docs

| Doc | Role |
|---|---|
| [`STATUS.md`](STATUS.md) | Shipped vs roadmap |
| [`WIRE.md`](WIRE.md) | Message types + HELLO v5 |
| [`README.md`](README.md) | Docs hub |
| [`../README.md`](../README.md) | Build / requirements |
| [`../scratchpad.md`](../scratchpad.md) | Session memory |
| [`superpowers/specs/2026-06-23-android-extended-display-design.md`](superpowers/specs/2026-06-23-android-extended-display-design.md) | Original design (historical) |
