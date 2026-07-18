# droppix — scratchpad

## Project identity

Spacedesk-like system: turn an Android tablet (or Linux desktop client) into a **true extended monitor** for a Linux PC (KDE Plasma / KWin first; X11 backend also shipped), with touch, stylus, keyboard, orientation, mirror/extend, and audio out. Verified E2E on Nexus 10 (Android 5.1.1) + Plasma 6 over USB and WiFi.

Remote: `git@github.com:Spinjitsudoom/droppix.git` · License: MIT

## Current state (as of 2026-07-18)

- **Status:** early but working; ~391 commits on `master` (tip `c1ef3cc`, VAAPI/NVENC cascade).
- **Host:** C++17 `droppix_stream` + Qt6 `droppix_gui`; evdi capture; AutoEncoder NVENC→VAAPI→x264; uinput input; PipeWire audio; DesktopBackend (KWin / X11 / Generic).
- **Android client:** Kotlin, MediaCodec, touch/stylus/keys/orientation/audio; minSdk 21; HELLO v5 settings.
- **Desktop client:** Qt6 Linux receive client in `client/` (decode-only).
- **Packaging:** AppImage + Flatpak (host + client); Android APK script.
- **macOS:** archived under `macos/` — not in build.
- **Docs (refreshed 2026-07-18):** living status in [`docs/STATUS.md`](docs/STATUS.md); protocol in [`docs/WIRE.md`](docs/WIRE.md); hub [`docs/README.md`](docs/README.md); lessons scaffold [`docs/lessons/`](docs/lessons/). Spec Status lines updated to Shipped/Partial.
- **Open roadmap:** cross-desktop M2 Sway / M3 GNOME Wayland.

## Key decisions (do not re-debate)

| Decision | Choice | Why |
|---|---|---|
| Display mode | Extend (evdi virtual monitor); mirror optional | Headline use case; KWin/X11 treat it as real output |
| Capture | libevdi direct (not PipeWire portal) | No portal popup; dirty rects; lower latency |
| Host language | C++ | Direct libevdi / VAAPI / uinput |
| Codec | H.264, in-band SPS/PPS on every IDR; AutoEncoder | Universal MediaCodec decode; GPU when available |
| Android | Native Kotlin + MediaCodec | Latency + stylus pressure access |
| Transport | Same TCP framing over adb reverse, WiFi (mDNS+TLS PIN), USB tether, AOA | One protocol, many carriers |
| Desktop coupling | `DesktopBackend` seam; KWin + X11 shipped | Narrow compositor surface; Sway/GNOME still open |
| Packaging reality | Needs host `evdi`, polkit/pkexec, PipeWire, avahi, adb | Kernel module + root uinput cannot live fully sandboxed |

## Architecture (at a glance)

```
evdi → Capturer → Encoder (NVENC|VAAPI|x264) → TransportServer
                      ↑                              ↕ wire protocol v5
              InputInjector (uinput)          Android / desktop client
```

## File map

| Path | Role |
|---|---|
| `host/src/` | Core: protocol, evdi, encoders, transport, AOA, tether, input, audio, desktop_backend |
| `host/gui/` | Qt6 control panel |
| `android/` | Kotlin tablet client |
| `client/` | Qt6 Linux receive client |
| `packaging/` | AppImage, Flatpak, APK |
| `CLAUDE.md` / `AGENTS.md` | Agent entrypoints |
| `.claude/rules/` | Canonical project rules (docs-maintenance, agent-tooling-sync) |
| `.cursor/rules/*.mdc` | Symlinks → `.claude/rules/` |
| `.claude/skills/` | Canonical project skills (Cursor mirrors under `.cursor/skills/`) |
| `scripts/check-agent-sync.sh` | Enforce Claude ↔ Cursor symlink sync |
| `docs/STATUS.md` | Living feature status |
| `docs/WIRE.md` | Current protocol summary |
| `docs/superpowers/specs/` | Design specs |
| `docs/superpowers/plans/` | Implementation plans |
| `docs/lessons/` | Indexed lessons |
| `macos/` | Archived experimental backend |
| `spike/aoa/` | AOA USB spike |

## Active / recent work

- Docs brought current (2026-07-18): README, STATUS, WIRE, spec Status lines, lessons scaffold, client README.
- HW encode shipped on master.
- Feature branches on remote are stale vs master (PRs already merged/closed).

## Hardcoded constraints

- Streamer often needs root (`pkexec`) for uinput + evdi.
- AppImage relocates streamer to `~/.local/share/droppix/runtime/`.
- Flatpak uses `flatpak-spawn --host`.
- Primary path: Plasma 6 / KWin / Wayland + evdi; X11 backend available.

## Links

- [README.md](README.md)
- [docs/STATUS.md](docs/STATUS.md)
- [docs/WIRE.md](docs/WIRE.md)
- [docs/README.md](docs/README.md)
- [Cross-desktop roadmap](docs/superpowers/specs/2026-07-05-cross-desktop-portability-design.md)

## Recent session notes

- **2026-07-18:** Claude ↔ Cursor tooling sync: canonical rules/skills under `.claude/`; `.cursor/rules/*.mdc` and `.cursor/skills/<name>` are symlinks. Always-on rules: `docs-maintenance`, `agent-tooling-sync`. Verify with `scripts/check-agent-sync.sh`.
- **2026-07-18:** Full docs refresh. Added living STATUS/WIRE, updated README for HW encode + `client/` + transports, marked shipped specs, scaffolded `docs/lessons/`, added `client/README.md`. Only open roadmap called out: Sway/GNOME backends.
