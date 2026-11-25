# L-2026-11-25-appimage-glibc-floor: the AppImage only ran on distros as new as the build box

- **ID:** `L-2026-11-25-appimage-glibc-floor`
- **Tags:** `packaging`, `appimage`, `portability`, `silent-failure`, `high`, `lesson`
- **Date:** 2026-11-25

## Symptom

Found while preparing a GNOME test VM (Ubuntu 24.04). The shipped AppImage's bundled libraries
needed `GLIBC_2.43` and its GUI needed `Qt_6.11`, so it couldn't start on Ubuntu, Debian,
openSUSE Leap or older Fedora. The distro table we gave the user marked those as "expected" to
work.

## Root cause

The binaries and every bundled library were built in the Fedora 44 `droppix-dev` distrobox.
Binaries carry the glibc symbol versions of the box they were linked on, and the GUI linked
against Fedora 44's Qt 6.11. "Uses the host's Qt" only holds when the host's Qt is at least as
new as the build's.

## Fix

`packaging/portable/`: build on Ubuntu 22.04 (glibc 2.35, Qt 6.2) in a `droppix-oldbase`
distrobox, with FFmpeg 7.1, x264 and libevdi built from source.
- Bundle only those five libraries (`KEEP_LIBS`). Older copies of anything else would load
  ahead of the host's newer ones through our rpath and break the host's Qt.
- Bundle a Qt fallback under `usr/qt-fallback`. `AppRun` adds it to the library path only when
  `ldconfig` finds no `libQt6Widgets.so.6`, so a KDE host keeps its own Qt. The fallback
  excludes glib, xcb core, GL and the rest of the host stack, which were the source of the
  original bundled-Qt crash. It also skips Qt's GTK theme and eglfs plugins, which drag in
  GTK, libinput, and so on.

## How to detect this in the future

```bash
for f in AppDir/usr/bin/* AppDir/usr/lib/*.so*; do objdump -T "$f"; done | grep -o 'GLIBC_2\.[0-9]*' | sort -Vu | tail -1
objdump -T AppDir/usr/bin/droppix_gui | grep -o 'Qt_6[.0-9]*' | sort -Vu | tail -1
```

Then run `~/droppix-vms/apptest/check.sh` in `ubuntu:22.04`, `debian:12` and `fedora` containers.
