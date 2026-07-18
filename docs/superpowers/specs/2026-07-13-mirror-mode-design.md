# Mirror mode (host-side screen duplication)

**Date:** 2026-07-13
**Status:** Shipped on master.
**Roadmap:** tier T2 "Mirror mode (duplication)" (HOST).

## Summary

Let a connected monitor **mirror** (clone) the host's primary screen instead of **extending** it. droppix already creates an evdi virtual output and streams it; mirror mode keeps that whole pipeline and simply asks the compositor to make the evdi output a **replica of the primary** (KDE `replicationSource`, X11 `--same-as`). The tablet then shows a live copy of the host's primary screen, and touch maps onto the primary. A per-monitor **Extend/Mirror** toggle lives in the host GUI. **Host-only — no protocol, client, or Android change.**

## Why compositor-clone (not a new capture path)

droppix's entire capture/encode/stream pipeline is built on the evdi output. Instead of adding a separate "capture the primary" path (portal/PipeWire screencast — large new surface), mirror mode reuses everything by cloning primary→evdi at the compositor level. KDE exposes this natively (`kscreen-doctor -o` shows a `replication source` per output); X11 has `xrandr --same-as`.

## Decisions

| Question | Decision |
| --- | --- |
| Mechanism | Compositor clone (evdi replicates primary); keep capturing evdi. |
| Toggle location | **Host GUI**, per active monitor (beside the existing Stop control). Host-only, no protocol change. |
| Apply point | The **streamer** applies the layout at startup from a `--mirror` flag (it's the process that identifies the evdi output name). Toggling restarts that session with the new flag. |
| Resolution in mirror | The evdi output is **created at the primary's resolution** (the streamer queries the primary via `backend->outputs()` at startup), so it's a pixel-accurate 1:1 clone and touch maps 1:1 onto the primary — no post-creation mode switch that evdi might not support. The client's requested resolution is ignored while mirroring. |
| Compositors | KDE/Wayland (`kscreen-doctor replicationSource`) + X11 (`xrandr --same-as`); Generic logs "mirror unsupported". |
| Persistence | Mode is remembered per session so reconnects keep it. Default **Extend**. |

## Backend (`host/src/desktop_backend.{h,cpp}`)

- Add `enum class LayoutMode { Extend, Mirror };`.
- Add a **pure, unit-testable** command builder so the exact compositor command is testable without a live compositor:
  `std::string layout_command(BackendKind kind, const std::string& evdi_output, const std::string& primary_output, int primary_id, const OutputInfo& primary_geom, LayoutMode mode);`
  - **KWin + Mirror:** `kscreen-doctor "output.<evdi>.replicationSource.<primary_id>"` (evdi was already created at the primary's resolution, so no mode switch — just replicate).
  - **KWin + Extend:** `kscreen-doctor "output.<evdi>.replicationSource.0" "output.<evdi>.position.<x>,<y>"` (clear replication, place right-of primary).
  - **X11 + Mirror:** `xrandr --output <evdi> --same-as <primary>` (evdi already at primary res → 1:1 clone).
  - **X11 + Extend:** `xrandr --output <evdi> --auto --right-of <primary>` (today's placement).
  - **Generic:** empty string (caller logs "mirror unsupported on this compositor").
  - All output/primary names go through `safe_output_name` before interpolation.
- Add `virtual bool apply_layout(const std::string& evdi_output, LayoutMode mode)` to `DesktopBackend`. Each backend finds the primary (X11: `xrandr --query` ` connected primary`; KWin: `kscreen-doctor -o` priority-1 output → its numeric id + geometry), builds the command via `layout_command`, runs it through `user_session_prefix()`, and returns whether the layout changed. `GenericBackend::apply_layout` logs and returns false.
  - The existing X11 `adopt_output` (reverse-PRIME provider link) still runs first; `apply_layout` replaces only the placement step (`--same-as` vs `--right-of`).

## Streamer (`host/src/stream_daemon.{h,cpp}` + CLI)

- CLI: `droppix_stream` gains `--mirror` (default off) → `cfg_.mirror` (`StreamConfig`).
- **evdi resolution:** when `cfg_.mirror`, before creating the virtual display the streamer queries `backend->outputs()`, finds the primary, and creates the evdi output at the **primary's WxH** (instead of the client's requested resolution) — so the replica is a 1:1 clone and no post-creation mode switch is needed. In extend mode the client's resolution is used as today.
- In `stream_daemon`, after the droppix output is identified and adopted (the existing `adopt_output` point), call `backend->apply_layout(droppix.name, cfg_.mirror ? LayoutMode::Mirror : LayoutMode::Extend)`, then re-query geometry (so touch mapping / encode size follow the new layout). Re-applied once per session start, so a reconnect (restart) reasserts the mode — mirrors how `adopt_output`/orientation are already applied at startup.
- Touch geometry (`injector.set_geometry`) uses the re-queried droppix geometry, which in mirror equals the primary's rect → touch lands on the primary.

## Host GUI (`host/gui/session_manager.h`, `args_builder.{h,cpp}`, `main_window.{h,cpp}`)

- `Session` gains `bool mirror = false`.
- `args_builder` appends `--mirror` when the session's `mirror` is true (mirror the existing flag-append style; `port`/`touch_name` pattern).
- `main_window`: add an **Extend/Mirror** control (a checkbox or two-state button) that acts on the **selected active monitor** (same selection the Stop control uses). Toggling: flip `session.mirror`, then stop + restart that session's `StreamController` with rebuilt args (the established start path). Reflect the current mode in the monitor's list label (e.g. "… — Mirror").
  - If no compositor support (Generic backend) the toggle still sends `--mirror`; the streamer logs "unsupported" and stays extended — acceptable (KDE/X11 are the targets).

## Testing

- **Pure `layout_command`** unit tests (`host/tests/`): for each `{KWin, X11} × {Extend, Mirror}`, assert the produced command contains the right verb — Mirror → `replicationSource.<id>` / `--same-as`; Extend → `replicationSource.0` / `--right-of`; and that a crafted unsafe output name is rejected/sanitized. Generic → empty.
- **`args_builder`** unit test: `--mirror` present iff `session.mirror` (C++ `test` in `host/gui/tests`).
- **`cfg` parse** unit test: `--mirror` sets `cfg_.mirror`.
- **On-device** (KDE + X11): toggle Mirror on an active monitor → the tablet shows a clone of the host's primary at its resolution; toggle Extend → the tablet returns to a separate second screen positioned right-of; touch controls the primary in mirror and the extended desktop in extend; a reconnect preserves the chosen mode. `apply_layout`'s live compositor effect is verified here (not unit-testable).

## Out of scope

- A "capture the primary directly" path (portal/PipeWire) — the compositor clone reuses the evdi pipeline.
- Per-region / partial mirror, mixed same-desktop tiling (that's the T4 video wall).
- Non-KDE-non-X11 compositors (Generic logs unsupported).
- Any protocol / client / Android change (mirror is a host GUI toggle).
