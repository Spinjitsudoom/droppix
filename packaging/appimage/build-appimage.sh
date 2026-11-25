#!/usr/bin/env bash
# Build the droppix AppImage.
#
# What it bundles: BOTH binaries (droppix_gui + droppix_stream) and the app-specific
# codec/streaming libraries a host might not have at the right version — ffmpeg
# (avcodec/avutil/swscale + their codec deps), x264, OpenSSL, libevdi.
#
# What it deliberately does NOT bundle: Qt6 and the low-level display/GPU/glib/system
# stack (glib, xcb, X11, GL/EGL, wayland, drm, va, fontconfig, systemd, ...). droppix
# REQUIRES KDE Plasma at runtime (it drives kscreen-doctor / qdbus), so every host that
# can run it already ships a matching Qt6 + that stack. Bundling Qt on top of the host's
# causes hard ABI crashes (double-loaded glib/xcb kill the Qt xcb platform plugin during
# dlopen), so we rely on host Qt. See docs/.../2026-07-02-fat-appimage-design.md.
#
# Root/evdi note: the bundled droppix_stream carries RPATH $ORIGIN/../lib (set by
# linuxdeploy). At runtime the GUI relocates it + usr/lib to ~/.local/share/droppix/
# runtime/ (see MainWindow::resolveStreamBin) so the pkexec-as-root evdi path works
# despite the AppImage's FUSE mount, and the extracted binary finds its libs via rpath.
#
# Build the binaries in the droppix-dev distrobox first; run THIS on the host (appimagetool
# needs `file`, absent in the distrobox). linuxdeploy + patchelf run inside the distrobox.
#
#   distrobox enter droppix-dev -- bash -lc 'cmake --build "$HOME/droppix-build" -j'
#   bash packaging/appimage/build-appimage.sh
#
# Env overrides: DROPPIX_BUILD_DIR (default ~/droppix-build), OUT, APPIMAGETOOL,
# DROPPIX_DISTROBOX, DROPPIX_DEPS_LIB (extra lib dir for linuxdeploy, e.g. the old-base
# /opt/droppix-deps/lib), QT_FALLBACK=1 (also bundle Qt under usr/qt-fallback, used ONLY when
# the host has no Qt 6; see packaging/portable/). For portable builds use
# packaging/portable/build-portable.sh, which sets these.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${DROPPIX_BUILD_DIR:-$HOME/droppix-build}"
APPIMAGETOOL="${APPIMAGETOOL:-$HOME/bin/appimagetool}"
OUT="${OUT:-$BUILD_DIR/droppix-x86_64.AppImage}"
CACHE="$HOME/.cache/droppix-appimage"
APPDIR="$BUILD_DIR/AppDir"
DISTROBOX="${DROPPIX_DISTROBOX:-droppix-dev}"

GUI_BIN="$BUILD_DIR/droppix_gui"
STREAM_BIN="$BUILD_DIR/droppix_stream"
ICON="$REPO/host/icons/droppix-256.png"

[ -x "$GUI_BIN" ]    || { echo "error: $GUI_BIN missing — build it first" >&2; exit 1; }
[ -x "$STREAM_BIN" ] || { echo "error: $STREAM_BIN missing — build it first" >&2; exit 1; }
[ -x "$APPIMAGETOOL" ] || { echo "error: appimagetool not found at $APPIMAGETOOL" >&2; exit 1; }
mkdir -p "$CACHE"
# DROPPIX_DISTROBOX=none runs every step directly (CI on an ubuntu-22.04 runner).
inbox() { if [ "$DISTROBOX" = none ]; then bash -lc "$1"; else distrobox enter "$DISTROBOX" -- bash -lc "$1"; fi; }

# --- tooling: linuxdeploy (bundles ldd deps + sets $ORIGIN rpaths) + patchelf ---
LD="$CACHE/linuxdeploy-x86_64.AppImage"
if [ ! -x "$LD" ]; then
  echo "fetching linuxdeploy..."
  curl -fsSL -o "$LD" "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
  chmod +x "$LD"
fi
inbox 'command -v patchelf >/dev/null || sudo dnf install -y patchelf || sudo apt-get install -y patchelf'

# --- stage icon + desktop ---
cp "$ICON" "$BUILD_DIR/droppix.png"
cat > "$BUILD_DIR/droppix.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Droppix
Comment=Use a tablet as a second monitor
Exec=droppix_gui
Icon=droppix
Categories=Utility;
Terminal=false
EOF

# --- populate the AppDir (inside the distrobox: has patchelf) ---
rm -rf "$APPDIR"; mkdir -p "$APPDIR"
inbox "
  export APPIMAGE_EXTRACT_AND_RUN=1 NO_STRIP=1
  export LD_LIBRARY_PATH='${DROPPIX_DEPS_LIB:-}'\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}
  cd '$BUILD_DIR'
  '$LD' --appimage-extract-and-run --appdir AppDir \
    -e droppix_gui -e droppix_stream -i droppix.png -d droppix.desktop
"

# --- host-served web PWA (static assets for --web-root) ---
# Refreshing web/dist is a nicety, not a requirement: web/dist is committed exactly so a
# packager needs no Node. Never let this abort the build (set -e is on) — `npm ci` cannot
# even succeed when the repo sits on a CIFS mount, which does not support the symlinks npm
# creates in node_modules/.bin (ENOTSUP), and that failure used to kill the whole AppImage.
if command -v npm >/dev/null 2>&1; then
  if ! (cd "$REPO/web" && npm ci && npm run build); then
    echo "warning: web rebuild failed; packaging the committed web/dist instead" >&2
  fi
fi
if [ -f "$REPO/web/dist/index.html" ]; then
  mkdir -p "$APPDIR/usr/share/droppix/web"
  cp -a "$REPO/web/dist/." "$APPDIR/usr/share/droppix/web/"
  echo "bundled web PWA into usr/share/droppix/web"
else
  echo "warning: web/dist missing — AppImage will not serve the PWA until built" >&2
fi

# --- prune the host-provided stack (keep ffmpeg/x264/openssl/evdi + their codec deps) ---
LIBDIR="$APPDIR/usr/lib"
rm -f  "$APPDIR/usr/bin/qt.conf"
rm -rf "$APPDIR/usr/plugins"
for pat in 'libQt6*' \
           'libglib-2*' 'libgobject-2*' 'libgio-2*' 'libgmodule-2*' 'libglycin*' \
           'libcairo*' 'libgdk*' 'libpango*' 'libharfbuzz*' 'libgraphite2*' \
           'libfreetype*' 'libfontconfig*' 'libpng16*' 'libpixman*' \
           'libxcb*' 'libX11*' 'libXau*' 'libXdmcp*' 'libxkbcommon*' 'libX*' \
           'libGL*' 'libEGL*' 'libGLX*' 'libGLdispatch*' 'libOpenGL*' 'libglvnd*' \
           'libwayland*' 'libgbm*' 'libdrm*' 'libva*' \
           'libexpat*' 'libbrotli*' 'libbz2*' 'libz.so*' \
           'libmount*' 'libblkid*' 'libselinux*' 'libsystemd*' 'libudev*' 'libcap*' \
           'libffi*' 'libpcre.so*' 'libicu*' 'libmd4c*' 'libdouble-conversion*'; do
  rm -f "$LIBDIR"/$pat
done
# Portable builds keep ONLY an allowlist (KEEP_LIBS: space-separated soname prefixes). Built
# on an old base, every other library here is an OLDER copy of something the host has; loaded
# first through our rpath, it would shadow the host's newer one and break the host's Qt.
if [ -n "${KEEP_LIBS:-}" ]; then
  for f in "$LIBDIR"/*; do
    base="$(basename "$f")"; keep=0
    for k in $KEEP_LIBS; do case "$base" in "$k".so*) keep=1 ;; esac; done
    [ $keep = 1 ] || rm -f "$f"
  done
fi
echo "bundled libs kept: $(ls "$LIBDIR" | wc -l) ($(ls "$LIBDIR" | tr '\n' ' '))"

# --- optional Qt fallback for hosts with no Qt 6 at all (stock GNOME/Sway installs) ---
# Kept OUT of usr/lib (the GUI relocates usr/lib/*.so* for the root streamer) and only put on
# the library path by AppRun when the host lacks libQt6Widgets: a KDE host keeps using its own
# Qt, which is what avoided the double-loaded glib/xcb crash in the first place.
if [ "${QT_FALLBACK:-0}" = 1 ]; then
  inbox "bash '$REPO/packaging/portable/bundle-qt-fallback.sh' '$APPDIR/usr/qt-fallback'"
  rm -f "$APPDIR/AppRun"
  cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
# droppix AppRun: the host's Qt 6 when it has one, else the bundled fallback.
HERE="$(dirname "$(readlink -f "$0")")"
if ! { /sbin/ldconfig -p 2>/dev/null || ldconfig -p 2>/dev/null; } | grep -q 'libQt6Widgets\.so\.6 '; then
  export LD_LIBRARY_PATH="$HERE/usr/qt-fallback/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export QT_PLUGIN_PATH="$HERE/usr/qt-fallback/plugins"
  export DROPPIX_QT_FALLBACK=1
  { /sbin/ldconfig -p 2>/dev/null || ldconfig -p 2>/dev/null; } | grep -q 'libOpenGL\.so\.0 ' ||
    export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$HERE/usr/qt-fallback/gl"
fi
exec "$HERE/usr/bin/droppix_gui" "$@"
APPRUN
  chmod +x "$APPDIR/AppRun"
  echo "bundled Qt fallback: $(du -sh "$APPDIR/usr/qt-fallback" | cut -f1)"
fi

# --- package (host: appimagetool needs `file`; use a cached runtime if present) ---
RT="$CACHE/runtime-x86_64"
rm -f "$OUT"
if [ -s "$RT" ]; then
  ARCH=x86_64 "$APPIMAGETOOL" --runtime-file "$RT" "$APPDIR" "$OUT"
else
  ARCH=x86_64 "$APPIMAGETOOL" "$APPDIR" "$OUT"   # appimagetool fetches its runtime
fi
echo "built: $OUT ($(stat -c%s "$OUT") bytes)"

# Drop a copy into the repo's "complete builds/" folder (git-ignored), named by date +
# short commit so builds are traceable and don't clobber each other.
DEST="$REPO/complete builds"
mkdir -p "$DEST"
SHA="$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo nogit)"
FINAL="$DEST/droppix-$(date +%Y%m%d-%H%M)-$SHA-x86_64.AppImage"
cp "$OUT" "$FINAL"
chmod +x "$FINAL" 2>/dev/null || true
echo "delivered: $FINAL"
