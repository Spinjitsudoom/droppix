# Fat AppImage (bundled dependencies) with evdi-capable root streamer — design

**Date:** 2026-07-02
**Status:** approved

## Goal

Ship a self-contained droppix AppImage that bundles Qt6 + codec/TLS/evdi libraries and
**both** binaries, and make the evdi extended-monitor (root) path work from the AppImage.
Flatpak is out of scope (its sandbox can't run the root/uinput/evdi/pkexec core).

## Constraints (why this shape)

- droppix links: Qt6 (Core/Gui/Widgets/Network/DBus) + OpenSSL (GUI); ffmpeg
  (avcodec/avutil/swscale) + x264 + OpenSSL + libevdi + libdrm + libva (streamer).
- droppix shells out to host-integration tools that CANNOT be bundled usefully: `pkexec`
  (setuid root), `kscreen-doctor`/`qdbus` (host KWin), `avahi-*` (host daemon),
  `parec`/`pw-record` (host PipeWire), `adb`. These stay on the host.
- **The evdi kernel module cannot be shipped** (kernel-space); it must be DKMS-installed on
  the host. libevdi (userspace) is bundled but inert without the module.
- **Root-from-AppImage problem:** the streamer runs as root via `pkexec`, but an AppImage's
  binaries live on a per-run FUSE mount at a random path. Root usually can't read that mount
  (`allow_other` off), and the random path breaks the permanent polkit rule (which matches an
  exact program path). So the bundled streamer must be relocated to a stable, real path.

## Components

### 1. Streamer relocation — `MainWindow` / `streamBin_`

Add `resolveStreamBin()` (replaces the fixed `applicationDirPath()/droppix_stream`):

- **Not an AppImage** (dev/build): `applicationDirPath() + "/droppix_stream"` (unchanged).
- **AppImage** (`$APPDIR` set): copy the bundled streamer to a stable location
  `~/.local/share/droppix/runtime/` on startup, and use that:
  - `runtime/bin/droppix_stream`   ← `$APPDIR/usr/bin/droppix_stream`
  - `runtime/lib/*`                ← `$APPDIR/usr/lib/*`
  - Re-copy only when missing or older than the AppImage's copy (mtime check).
  - `streamBin_ = runtime/bin/droppix_stream`.
  - The bundled streamer's RPATH is `$ORIGIN/../lib` (set by linuxdeploy), so from
    `runtime/bin/` it resolves bundled libs in `runtime/lib/` — no `LD_LIBRARY_PATH`, no
    wrapper, works as root (the binary is not setuid, so `$ORIGIN` RPATH is honored).

`build_command` already does `pkexec <streamBin_> …` for evdi and `<streamBin_> …` otherwise,
so both paths use the stable binary. `setupAuth`'s polkit rule already matches `streamBin_`
(with the /home↔/var/home alias), now a stable path — permanent auth works.

### 2. Packaging — `packaging/appimage/build-appimage.sh` (rewrite: fat)

- Ensure `patchelf` is present in the `droppix-dev` distrobox (`sudo dnf install -y patchelf`).
- Fetch `linuxdeploy` + `linuxdeploy-plugin-qt` (cache under `~/.cache/droppix-appimage/`) if
  missing (network).
- In the distrobox (has Qt6 + qmake6 + patchelf), run linuxdeploy to POPULATE the AppDir:
  `linuxdeploy --appdir <AppDir> -e droppix_gui -e droppix_stream -i <icon> -d <desktop>
   --plugin qt` with `QMAKE=/usr/bin/qmake6`. This bundles Qt libs + platform/wayland/tls/
  imageformat plugins + all ldd deps for both binaries and sets `$ORIGIN`-relative RPATHs.
- On the host (has `file`), run `appimagetool <AppDir> <out>.AppImage` (FUSE-free squashfs).
- AppRun (linuxdeploy default) launches `droppix_gui`. Keep the desktop/icon from `host/icons`.

The build runs from the host and shells into the distrobox for the linuxdeploy step.

### 3. Docs — README "Requirements"

A section listing host prerequisites the AppImage cannot provide: evdi kernel module (DKMS),
polkit/pkexec, KDE Plasma (kscreen-doctor, qdbus), PipeWire (parec), avahi-daemon, adb (USB).
Note that USB/WiFi test-pattern work without evdi; the extended monitor needs the evdi module.

## Testing

- Build the AppImage; launch it on the host → GUI opens.
- `ldd` inside the extracted AppDir shows Qt/ffmpeg/x264/openssl resolving to bundled `usr/lib`.
- Run the AppImage, confirm `~/.local/share/droppix/runtime/bin/droppix_stream` is created and
  `ldd` on it resolves bundled libs via RPATH.
- evdi Start (pkexec the stable path) + on-device streaming: manual verification.

## Out of scope (YAGNI)

- Flatpak.
- Bundling host-service CLIs (adb/kscreen/qdbus/avahi/parec) — they must talk to host daemons.
- Auto-installing the evdi kernel module (DKMS is a host/distro concern; README documents it).
