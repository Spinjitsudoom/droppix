# Configurable Virtual-Monitor Resolution + Refresh (evdi) — Design

**Date:** 2026-06-24
**Status:** Shipped on master.
**Scope:** Make the evdi virtual monitor's resolution and refresh rate
configurable (today hard-wired to 1920×1080@60). Engine + streamer flags + host
GUI. No Android-app or wire-protocol change.

## Goal

Let the user choose the virtual monitor's **resolution** and **refresh rate** for
the evdi (real-monitor) source — via the host GUI (dropdowns) and the
`droppix_stream` CLI (flags) — instead of the fixed 1080p60. The chosen refresh
is the monitor's EDID-advertised refresh; it is **separate** from the existing
capture/stream FPS.

## Decisions (settled during brainstorming)

| Decision | Choice |
|---|---|
| Timing generation | **VESA CVT reduced-blanking** computed in code (not a hand-built table), so resolution × refresh combinations are handled uniformly |
| Refresh semantics | **Separate** EDID/monitor refresh (`--refresh`) and capture FPS (`--fps`) — two independent controls |
| Source scope | **evdi only** (the test-pattern source already has a resolution control) |
| Default/fallback | Keep the verified `timing_1080p60()` CEA preset as the default and as the path for the common 1080p60 case; CVT is used for other modes |
| Wire protocol | **Unchanged** — CONFIG already carries the actual encode dimensions, so the Android app auto-adapts |

## Engine: CVT timing module

New pure module `host/src/cvt.{h,cpp}`:

```cpp
// VESA CVT reduced-blanking v1. Returns a Timing (host/src/edid.h) for the mode.
// h_active is rounded down to the 8-pixel cell granularity (CVT requirement).
droppix::Timing cvt_rb_timing(int width, int height, int refresh_hz);
```

It computes pixel clock, horizontal/vertical active/blank/front/sync from the CVT
reduced-blanking algorithm and returns the existing `Timing` struct. The existing
`build_edid(const Timing&)` is unchanged and consumes it. `timing_1080p60()` is
retained; the evdi source uses it directly when the requested mode is exactly
1920×1080@60 (known-good CEA timing), and `cvt_rb_timing` otherwise.

**Why CVT:** it's how `xrandr`/`cvt` generate custom modes, so KWin/DRM accept the
resulting detailed timing. Correctness is verified by unit tests comparing against
the reference `cvt -r` tool's published Modelines.

## evdi source + streamer flags

- `EvdiFrameSource` gains construction parameters `(int width, int height, int refresh_hz)`.
  `start()` builds the EDID from the chosen mode (`timing_1080p60()` for
  1920×1080@60, else `cvt_rb_timing(w,h,r)`), connects evdi, waits for KWin's mode,
  and returns the actual captured dimensions.
- `stream_main`: `--width` / `--height` now also apply to the **evdi** source
  (today they are honored only by the test-pattern source); add `--refresh N`
  (default 60). These are passed into `EvdiFrameSource`. Defaults preserve current
  behavior: no flags → 1920×1080@60.

## Data flow

```
mode (W×H@R) → cvt_rb_timing (or timing_1080p60 for 1080p60) → build_edid
            → evdi connect → KWin sets W×H@R → Capturer (captures real mode)
            → Encoder (W×H, capture fps) → CONFIG(W×H) → Android app adapts
```

## Host GUI changes

- **Resolution dropdown enabled for the evdi source** (currently disabled for
  evdi). Presets cover 16:9 and the Nexus 10's native 16:10:
  - 16:9: 1280×720, 1920×1080, 2560×1440
  - 16:10: 1280×800, 1920×1200, 2560×1600
- **New "Refresh" dropdown:** 30, 60 Hz (the EDID/monitor refresh), alongside the
  existing **FPS** spinbox (capture rate).
- `Settings` gains `int refresh_hz` (default 60); `ProfileStore` persists it.
- `ArgsBuilder`: for the evdi source, now emits `--width`, `--height`, and
  `--refresh`. The test-pattern path is unchanged.

The engine (CVT) accepts arbitrary values, so the CLI is not limited to the GUI's
preset list; the dropdowns are just curated common modes.

## Error handling

- If KWin will not set a requested mode, `EvdiFrameSource::start` (via
  `wait_for_mode` timeout) returns false; the stream fails cleanly and the GUI
  surfaces the error in its log. The user picks a different mode. No crash.
- `cvt_rb_timing` always returns a `Timing`; `h_active` is rounded to the cell
  granularity. The exact 1920×1080@60 case bypasses CVT and uses the verified CEA
  preset, so the common path is unaffected by any CVT edge case.
- Out-of-range refresh / zero dimensions are clamped/validated at the CLI and GUI
  (GUI offers only valid presets; CLI defaults to 1080p60 on absent flags).

## Testing strategy

- **`cvt_rb_timing` (unit, GoogleTest):** computed pixel clock and
  horizontal/vertical active/blank/front/sync match the reference `cvt -r`
  Modelines for 1920×1080@60, 1280×720@60, 2560×1600@60, 1920×1200@60, and
  1920×1080@30 (within documented rounding).
- **`build_edid` of a CVT timing (unit):** the DTD encodes the chosen
  `h_active`/`v_active`; checksum valid.
- **`ArgsBuilder` (unit):** evdi source now includes `--width/--height/--refresh`;
  test-pattern source unchanged.
- **Operator (live):** in the GUI, pick a non-1080p mode and a refresh → Start →
  confirm KWin sets that mode on the droppix monitor and the tablet displays it (a
  16:10 mode fills the Nexus 10 with no letterboxing).

## Components & boundaries

| Unit | What it does | Depends on |
|---|---|---|
| `cvt` | mode → `Timing` (pure math) | — |
| `edid` (existing) | `Timing` → 128-byte EDID | — |
| `EvdiFrameSource` | mode → EDID → evdi monitor → frames | `cvt`, `edid`, `VirtualDisplay`, `Capturer` |
| `stream_main` | parse `--width/--height/--refresh`, wire the source | `EvdiFrameSource` |
| GUI `Settings`/`ArgsBuilder`/`MainWindow`/`ProfileStore` | choose + persist + pass the mode | the streamer flags |

## Out of scope

- Test-pattern source refresh handling (it just uses FPS + a generated pattern).
- Arbitrary custom mode entry in the GUI (dropdown presets only; the CLI accepts
  arbitrary values for power users).
- Sending refresh to the Android app (the app decodes whatever arrives; refresh is
  a host/KWin concern).
- Interlaced modes; HDR/color metadata in the EDID.
