# Client-declared monitor grid (wall arrangement)

**Date:** 2026-07-16
**Status:** Shipped on master (2026-08-02). HELLO v6 (`wall_col`/`wall_row`) + host placement (`grid_position`/`place_output`, extend-only) + Android and Linux-client "Wall position" settings, all end-to-end. On-device 2-tablet grid verification pending (needs two same-model tablets). Update: wall placement is skipped in mirror mode (the output is overlaid on the primary there). Follow-up (2026-08-02): the web PWA client (`web/`) was also upgraded to HELLO v6 with toolbar "Wall" col/row inputs, so all three clients now declare a cell.
**Roadmap:** tier T4 "Video wall" — the **narrowed** interpretation the user chose: arrange the existing per-tablet monitors into a grid (NOT the single-framebuffer tiled-wall engine).

## Summary

Let each tablet declare a grid **cell** `(col, row)`; the host positions that tablet's evdi monitor at the corresponding spot in the extended desktop, so several tablets form a deliberately arranged grid (2×2, a vertical stack, …) instead of today's automatic "right-of primary." **Still N separate monitors — just arranged.** The cell is client-owned (sent in HELLO). Builds directly on multi-monitor (per-tablet evdi outputs) + the mirror-mode output-positioning infrastructure.

## Not this (out of scope)

- The **single large framebuffer tiled across tablets** (one evdi output, shared capture, per-tile crop) — the roadmap's heavy "video wall engine." The user explicitly chose "arrange existing monitors" instead.
- GUI-coordinated exact packing for **mixed-resolution** tablets; **bezel** compensation; per-cell rotation. (Follow-ups.)

## Decisions

| Question | Decision |
| --- | --- |
| Model | Arrange the existing per-tablet evdi monitors; each tablet is one monitor at its native resolution, placed at its declared grid cell. |
| Who declares the cell | The **client**, via HELLO (per-client). |
| Wire | HELLO **v6**: add `u16 wall_col, u16 wall_row` after the v5 `bitrate_kbps`. Version-gated; pre-v6 → `(0,0)`. Bump `kProtocolVersion` to 6. |
| Grid math | **Uniform grid** (each cell = the tablet's own resolution): `pos = (primary.right + col × own_w, primary.top + row × own_h)`. `(0,0)` reproduces today's right-of-primary placement. |
| Host placement | The streamer places its evdi output at the computed `(x, y)` via an explicit-position backend call (extends mirror-mode positioning), instead of the auto `--right-of`. |
| Mixed sizes / overlap | Best-effort; documented. Same-model tablets tile perfectly; distinct cells are the user's responsibility (two tablets on the same cell overlap). |

## Protocol (`host/src/protocol.{h,cpp}` + `Protocol.kt` + Linux client)

- **HELLO v6:** body gains `u16 wall_col, u16 wall_row` immediately after `bitrate_kbps` (before the u16-prefixed name/id strings).
  - `encode_hello(...)` gains trailing `uint16_t wall_col = 0, uint16_t wall_row = 0`.
  - `decode_hello(...)` gains `uint16_t& wall_col, uint16_t& wall_row`; version-gated — only read when `version >= 6`, else `0`. Keep the existing back-compat overloads working (thin forwarders discarding the new fields for pre-v6 callers).
  - Bump `kProtocolVersion` / `Protocol.VERSION` to **6**; document v6 back-compat in `WIRE.md`. Update the C++ + Kotlin HELLO test vectors.
- **Independent-message note:** unchanged — Touch/Scroll/Key/Pen/etc. are untouched.

## Host (`stream_daemon.cpp`, `desktop_backend.{h,cpp}`)

- `StreamConfig` gains `int wall_col = 0, wall_row = 0;` seeded from the HELLO decode (like `orientation`).
- **Pure grid math** (unit-tested): `Rect` or `struct Point { int x, y; }` `grid_position(int col, int row, int own_w, int own_h, int anchor_x, int anchor_y)` → `{ anchor_x + col*own_w, anchor_y + row*own_h }`.
- **Placement:** in `stream_daemon`, after identifying the droppix output, find the primary in `outputs()` (reuse the mirror-mode `.primary` field), compute `anchor = (primary.geom.x + primary.geom.w, primary.geom.y)` and `pos = grid_position(cfg_.wall_col, cfg_.wall_row, droppix.geom.w, droppix.geom.h, anchor.x, anchor.y)`, then place the output at `pos` via a new `DesktopBackend::place_output(output, x, y)`:
  - **X11:** `xrandr --output <N> --pos <x>x<y>` (replaces the `--right-of` step for the wall case; the reverse-PRIME provider link from `adopt_output` still runs).
  - **KWin:** `kscreen-doctor "output.<N>.position.<x>,<y>"` (same shape as the mirror-mode extend position).
  - **Generic:** no-op.
  - `place_output` reuses `safe_output_name` + the `user_session_prefix()` run pattern; `x`/`y` are ints via `std::to_string` (no injection surface). At `(0,0)` this equals the right-of-primary anchor (today's behavior preserved).
- Touch geometry (`set_geometry`) already uses the droppix output's rect (re-queried after placement), so touch lands correctly at the new position.

## Client (`AppSettings`/`ClientSettings` + UI + HELLO send)

- **Android:** `AppSettings` gains `wallCol: Int = 0, wallRow: Int = 0` (persisted); a **"Wall position"** row in Settings (two small number pickers / spinners, 0-based col & row); `TransportClient` HELLO send passes them (`encodeHello(..., wallCol, wallRow)`).
- **Linux client:** `ClientSettings` gains `wall_col = 0, wall_row = 0`; two `QSpinBox` in the settings dialog; `transport_client` HELLO passes them.
- Default `(0,0)` = current single-monitor / right-of behavior, so existing users are unaffected until they set a cell.

## Testing

- **Pure `grid_position`** unit test (C++): `(0,0)` → anchor; `(1,0,1280,800, 1920,0)` → `(3200,0)`; `(0,1)` → `(anchor_x, anchor_y+800)`; `(2,1)` → `(anchor_x+2*w, anchor_y+h)`.
- **HELLO v6 round-trip:** C++ `test_protocol` + Kotlin `ProtocolTest` — encode with `wall_col/row`, decode back; a **v5 body** decodes with `wall_col=wall_row=0` (back-compat); `kProtocolVersion==6`.
- **On-device:** two same-model tablets — tablet A cell `(0,0)`, tablet B cell `(1,0)` → they sit side-by-side as one row (drag a window A→B crosses cleanly); set B to `(0,1)` → it stacks below A; touch lands on the correct monitor; a single tablet at `(0,0)` behaves exactly as today.

## Out of scope

- Single-framebuffer tiled wall (shared-capture engine).
- Mixed-resolution exact packing (GUI-coordinated) and bezel gaps.
- Per-cell rotation; auto-detecting physical arrangement.
- No change to Touch/Scroll/Key/Pen or the streamer's capture/encode path.
