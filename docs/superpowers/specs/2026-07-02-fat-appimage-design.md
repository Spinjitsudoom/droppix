# droppix AppImage (bundled app dependencies) with evdi-capable root streamer — design

**Date:** 2026-07-02
**Status:** built & verified

## Goal

Ship a droppix AppImage that bundles **both** binaries plus the app-specific codec/streaming
libraries, and make the evdi extended-monitor (root) path work from the AppImage. Flatpak is
out of scope (its sandbox can't run the root/uinput/evdi/pkexec core).

## Qt is NOT bundled (important finding)

The original plan bundled Qt6 too. In practice that **crashes**: the bundled Qt xcb platform
plugin segfaults during `dlopen` because the bundled glib/xcb stack double-loads against the
host's (a hard ABI conflict on Fedora 44 / Qt 6.11; the canonical excludelist prune and
dropping the whole display stack did not fix it). The resolution is architectural, not a
workaround: **droppix requires KDE Plasma at runtime** (it drives `kscreen-doctor`/`qdbus`),
so every host that can run it already ships a matching Qt6 + glib/xcb/GL. Bundling Qt is both
the cause of the crash and redundant. So the AppImage uses **host Qt6** and bundles only the
app libs a host may lack at the right version: ffmpeg (avcodec/avutil/swscale + codec deps),
x264, OpenSSL, libevdi.

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

### 2. Packaging — `packaging/appimage/build-appimage.sh`

- Ensure `patchelf` in the `droppix-dev` distrobox; fetch `linuxdeploy` (cache under
  `~/.cache/droppix-appimage/`).
- In the distrobox (has patchelf), `linuxdeploy --appdir AppDir -e droppix_gui -e
  droppix_stream -i <icon> -d <desktop>` bundles all ldd deps for both binaries and sets
  `$ORIGIN/../lib` RPATHs. (No `--plugin qt` — we do not bundle Qt.)
- **Prune** the host-provided stack from `AppDir/usr/lib`: `libQt6*`, `usr/plugins`,
  `usr/bin/qt.conf`, and the display/GPU/glib/system libs (glib, gobject, gio, cairo,
  pango, harfbuzz, freetype, fontconfig, xcb, X11, GL/EGL, wayland, drm, va, xkbcommon,
  systemd/udev/selinux, expat, brotli, z, icu, ...). What remains: ffmpeg + codec deps,
  x264, OpenSSL, libevdi.
- On the host (has `file`), `appimagetool [--runtime-file <cached>] AppDir <out>.AppImage`.
  The type2 runtime is cached at `~/.cache/droppix-appimage/runtime-x86_64` (extractable from
  any prior AppImage via `--appimage-offset` + `head -c` when the GitHub download is blocked).

The script runs on the host and shells into the distrobox for the linuxdeploy step.

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
