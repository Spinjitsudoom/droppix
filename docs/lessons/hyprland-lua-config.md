# L-2026-11-25-hyprland-lua-config: `hyprctl keyword` says "unknown request" on current Hyprland

- **ID:** `L-2026-11-25-hyprland-lua-config`
- **Tags:** `host`, `desktop-backend`, `hyprland`, `gotcha`, `medium`, `lesson`
- **Date:** 2026-11-25

## Symptom

On Hyprland 0.57.2 (Arch), every placement/touch/pen command from `HyprlandBackend` logged
`unknown request`, while `hyprctl -j monitors` worked.

## Root cause

Hyprland 0.5x generates and prefers a **Lua** config (`~/.config/hypr/hyprland.lua`). With a Lua
config `hyprctl keyword` is unknown, and runtime changes go through `hyprctl eval '<lua>'`
using `hl.monitor({...})` and `hl.device({...})`. Older Hyprland, and users still on
`hyprland.conf`, only have `keyword`.

Separately: Hyprland **0.41** (Ubuntu 25.10, wlroots-based) sees the evdi output but fails with
"Couldn't commit output". **0.57** (aquamarine) adopts and modes it with no extra setup, not
even `WLR_EVDI_RENDER_DEVICE`.

## Fix

`hypr_set()` tries `keyword` and, on "unknown request", sends the Lua equivalent
(`hypr_place_lua` / `hypr_mirror_lua` / `hypr_device_lua`, unit-tested). VM-verified on 0.57.2:
taps, right-click and pen land on the droppix screen; placement, mirror and reconnects work.
