# GNOME Wayland backend — implementation plan

**Spec:** [`../specs/2026-11-25-gnome-wayland-backend-design.md`](../specs/2026-11-25-gnome-wayland-backend-design.md)
**Goal:** droppix identifies, places, mirrors, and binds touch/pen to its monitor on GNOME Wayland.
**Executed:** inline, overnight 2026-11-25. The user delegated decisions ("feel free to make the necessary decisions").

## Tasks

- [x] **1. Detection.** `BackendKind::Gnome`; `select_backend_kind(..., has_mutter)`. Mutter's D-Bus name is probed in `make_desktop_backend`. Tests: `DesktopBackend.GnomeWaylandSelectsGnome`, `MutterOnTheBusSelectsGnomeWithoutADesktopName`, `GnomeOnX11KeepsTheX11Backend`, `KdeBeatsMutter`.
- [x] **2. Per-session uinput ids.** `session_device_id(base, touch_name)` is used for the touch (0xd701) and pen (0xd704) devices. Tests: `SessionDeviceId.*`.
- [x] **3. GVariant text parser.** `gvariant_text.{h,cpp}`. Tests: `GVariantText.*`.
- [x] **4. Mutter state.** `mutter_state.{h,cpp}`: `parse_mutter_state`, `mutter_outputs` (logical geometry, rotation, disabled monitors), `mutter_find_by_serial`, `mutter_place_config`, `mutter_mirror_config`, `gnome_map_command`. Tests: `MutterState.*`, `MutterApply.*`, `GnomeMapCommand.*`.
- [x] **5. `GnomeBackend`.** `outputs` / `map_touch` / `map_pen` / `apply_layout` / `place_output` over `gdbus` + `gsettings`, through `user_session_prefix()`.
- [x] **6. VM verification (Ubuntu 24.04 / GNOME 46).** Run the real `droppix_stream` with `--touch`; scripted client (`~/droppix-vms/spike/fakeclient.py`); a GTK probe window per monitor logs where taps land. Check:
  - evdi adopted;
  - the backend is picked under sudo;
  - the output is identified;
  - touch lands on the droppix monitor;
  - right-click lands at the tap point;
  - placement;
  - mirror.
- [x] **7. Fixture from reality.** Add a captured `GetCurrentState` from the VM as a parser test.
- [ ] **8. Fedora (GNOME 49) pass.** If time allows.
- [x] **9. Docs.** STATUS, README, ARCHITECTURE, spec Status, roadmap M3 line, scratchpad, lessons.

## Rulings made during the build

- **Revive on reconnect (new hook).** Mutter left the old monitor "on", so the new connection never got a mode. Added `FrameSource` `on_stalled` and `DesktopBackend::revive_output` / `find_output`. Cost if wrong: a 3 s pause plus one off/on flicker at session start on GNOME when the first mode is slow.
- **No mirror EDID change.** The streamer already matches the primary's size in mirror mode.
- **Pen fix shipped with this backend** although it is cross-desktop. It was found here, and touching KDE behavior is limited to the pen device becoming visible to libinput.
