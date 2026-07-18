# CGVirtualDisplay open-source systems — research findings

**Date:** 2026-07-18  
**Type:** Research / findings (not a build plan).  
**Status:** Findings recorded. No Mac host work scheduled from this note alone.  
**Audience:** Future session that resumes a macOS **host** (PC role) using private `CGVirtualDisplay`.

## Goal

Survey open-source systems built on Apple’s private `CGVirtualDisplay` CoreGraphics API and recommend what droppix can **own and manage** if/when the archived [`macos/`](../../../macos/) backend is revived — without depending on proprietary DisplayLink Manager or BetterDisplay.

## Shared facts (all candidates)

- **API:** private classes inside CoreGraphics — typically `CGVirtualDisplayDescriptor`, `CGVirtualDisplayMode`, `CGVirtualDisplaySettings`, `CGVirtualDisplay`. Display lives while the object is retained.
- **No public Apple SDK** for “create a software extended monitor.”
- **Common limit:** ~60 Hz refresh (API, not a droppix choice).
- **Distribution:** private API → unsuitable for Mac App Store; fine for direct / Homebrew-style shipping (same constraint as DeskPad / BetterDisplay-class tools).
- **Does not solve:** touch/pen/keyboard injection, app audio routing, or droppix wire protocol. Create (+ often capture) only.

Linux already has the structurally better path (`evdi`). On macOS everyone approximates that with this private API.

## Candidate matrix

| Project | License | Form | Stars / maturity (approx.) | Fit for droppix |
|---|---|---|---|---|
| [Stengo/DeskPad](https://github.com/Stengo/DeskPad) | MIT | App (Swift + ObjC) | High (~7.7k★); canonical pioneer | **Reference** — crib shim headers + create/teardown patterns |
| [xocialize/VirtualDisplayKit](https://github.com/xocialize/VirtualDisplayKit) | MIT (DeskPad attribution) | Swift Package | Library-shaped; macOS 13+ SPM | **Best third-party package shape** if Swift FFI is acceptable |
| [pacifistazero/vdisplay](https://github.com/pacifistazero/vdisplay) | MIT | CLI + menu bar + kit | Very new (2026-07); small | Good **shim layout** crib (`CGVirtualDisplayShim` + `DisplayManager`) |
| [SamuelRioTz/SimpleDisplay](https://github.com/SamuelRioTz/SimpleDisplay) | Free / OSS | Menu bar + URL/CLI | Active OSS display utility | Poor **runtime** dependency (display dies with helper); OK for ideas |
| [huberdf/FreeDisplay](https://github.com/huberdf/FreeDisplay) | MIT | Full display-manager app | BetterDisplay-like scope | Overkill dependency; virtual display is one feature among many |
| [welfvh/daylight-mirror](https://github.com/welfvh/daylight-mirror) | MIT | Mac app + Android APK | Closest **product cousin** | Study capture path; not a drop-in library |
| [waydabber/BetterDisplay](https://github.com/waydabber/BetterDisplay) | Proprietary | App | Dominant commercial tool | **Do not depend** — GitHub is issues/releases; core source private |
| droppix [`macos/`](../../../macos/) | Project MIT | ObjC++ `MacVirtualDisplay` + `MacFrameSource` | Archived WIP; not in build | **Primary ownership target** |

## Integration models evaluated

| Model | Description | Verdict |
|---|---|---|
| **A. Own thin wrapper** | Keep/evolve `macos/` ObjC++; vendor DeskPad/vdisplay-style **shim headers** only; plug into `FrameSource` | **Recommended** |
| **B. Vendor VirtualDisplayKit** | SPM / fork; use create + ScreenCaptureKit/stream APIs | Optional later; Swift↔C++ friction for `droppix_stream` |
| **C. External helper** | Drive SimpleDisplay URL / BetterDisplay / CLI | Reject for product host — lifetime and permission UX wrong |

## Recommended ownership shape

```
macos/
  third_party/cgvirtualdisplay_shim/   # vendored private-API redeclarations (DeskPad/vdisplay lineage)
  src/macos_virtual_display.{h,mm}     # create / retain / teardown (exists)
  src/macos_frame_source.{h,mm}        # capture (exists; keep CGDisplayStream-first)
host/                                  # FrameSource seam only; no Swift SPM required for MVP
```

Hard rules if resumed:

1. **Do not** shell out to BetterDisplay or DisplayLink Manager.
2. **Do not** make the virtual display owned by a separate menu-bar process unless TCC isolation forces it later.
3. Prefer **direct distribution** packaging (same private-API reality as DeskPad).
4. Re-validate capture FPS: daylight-mirror reports ScreenCaptureKit ~30 fps on virtual displays and moved to **`CGDisplayStream`** for ~60 Hz — aligns with archived `MacFrameSource`.

## Priority reading order (next Mac-host session)

1. [`macos/README.md`](../../../macos/README.md) — what droppix already has and what was never built (input).
2. DeskPad — smallest mental model of registration + retain lifetime.
3. VirtualDisplayKit / vdisplay — packaged headers + manager patterns.
4. daylight-mirror — Mac extend → Android stream cousin; capture performance notes.

## Explicit non-goals (from this research)

- DisplayLink USB hardware / Manager as a backend.
- BetterDisplay as a hard dependency.
- Claiming a public Apple virtual-display API exists.
- Full Mac host parity plan (encode, input, audio, GUI) — separate design when scheduled.

## Related docs

| Doc | Role |
|---|---|
| [`macos/README.md`](../../../macos/README.md) | Archived Phase A status |
| [`2026-07-05-cross-desktop-portability-design.md`](2026-07-05-cross-desktop-portability-design.md) | Linux compositor portability (different OS) |
| [`docs/STATUS.md`](../../STATUS.md) | Living feature status |
| [`scratchpad.md`](../../../scratchpad.md) | Session memory |

## Decision locked by this note

If a macOS host is resumed: **own a thin `CGVirtualDisplay` wrapper under `macos/` (model A)**; treat DeskPad / VirtualDisplayKit / vdisplay / daylight-mirror as MIT crib sheets; reject BetterDisplay and DisplayLink as dependencies.
