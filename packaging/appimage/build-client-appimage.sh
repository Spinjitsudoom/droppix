#!/usr/bin/env bash
# Build the droppix CLIENT AppImage (droppix_client).
#
# What it bundles: droppix_client + the app/codec libraries a host might not have at the
# right version — ffmpeg (avcodec/avutil/swscale + their large external-codec closure) and
# OpenSSL.
#
# What it deliberately does NOT bundle: Qt6 and the low-level display/GPU/glib/xcb system
# stack. A tolerant experiment proved a fully-bundled Qt here SIGSEGVs at startup — the
# bundled Qt xcb platform stack clashes with the host's `libxcb.so.1`/`libGL`, which MUST
# come from the host (they can't be portably bundled). This is the same constraint the HOST
# AppImage documents. So the client AppImage relies on host Qt6 (present on this and any
# KDE/Qt6 machine; verified libQt6Multimedia 6.11 here). For a FULLY self-contained client,
# use the Flatpak (org.droppix.Client) instead — the KDE runtime supplies a consistent Qt6.
#
# Build the binary in the droppix-dev distrobox first; run THIS on the host (appimagetool
# needs `file`, absent in the distrobox). linuxdeploy + patchelf run inside the distrobox.
#
#   distrobox enter droppix-dev -- bash -lc 'cmake --build "$HOME/droppix-client-build" -j'
#   bash packaging/appimage/build-client-appimage.sh
#
# Env overrides: DROPPIX_CLIENT_BUILD_DIR (default ~/droppix-client-build), OUT, APPIMAGETOOL.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${DROPPIX_CLIENT_BUILD_DIR:-$HOME/droppix-client-build}"
APPIMAGETOOL="${APPIMAGETOOL:-$HOME/bin/appimagetool}"
OUT="${OUT:-$BUILD_DIR/droppix-client-x86_64.AppImage}"
CACHE="$HOME/.cache/droppix-appimage"
APPDIR="$BUILD_DIR/AppDir"
DISTROBOX="${DROPPIX_DISTROBOX:-droppix-dev}"

BIN="$BUILD_DIR/droppix_client"
ICON="$REPO/host/icons/droppix-256.png"

[ -x "$BIN" ]          || { echo "error: $BIN missing — build it first" >&2; exit 1; }
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
cp "$ICON" "$BUILD_DIR/droppix-client.png"
cat > "$BUILD_DIR/droppix-client.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Droppix Client
Comment=Use this PC as a second monitor for a droppix host
Exec=droppix_client
Icon=droppix-client
Categories=Utility;Network;
Terminal=false
EOF

# --- populate the AppDir (inside the distrobox: has patchelf) ---
rm -rf "$APPDIR"; mkdir -p "$APPDIR"
inbox "
  export APPIMAGE_EXTRACT_AND_RUN=1 NO_STRIP=1
  export LD_LIBRARY_PATH='${DROPPIX_DEPS_LIB:-}'\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}
  cd '$BUILD_DIR'
  '$LD' --appimage-extract-and-run --appdir AppDir \
    -e droppix_client -i droppix-client.png -d droppix-client.desktop
"

# --- prune the host-provided stack (keep ffmpeg/openssl + their codec deps) ---
# Same rationale as the host AppImage: rely on host Qt + host xcb/GL/glib. Bundling any of
# these on top of the host's copies double-loads glib/xcb and crashes the Qt xcb plugin.
LIBDIR="$APPDIR/usr/lib"
rm -f  "$APPDIR/usr/bin/qt.conf"
rm -rf "$APPDIR/usr/plugins"
for pat in 'libQt6*' \
           'libglib-2*' 'libgobject-2*' 'libgio-2*' 'libgmodule-2*' 'libglycin*' \
           'libcairo*' 'libgdk*' 'libpango*' 'libharfbuzz*' 'libgraphite2*' \
           'libfreetype*' 'libfontconfig*' 'libpng16*' 'libpixman*' 'librsvg*' \
           'libxcb*' 'libX11*' 'libXau*' 'libXdmcp*' 'libxkbcommon*' 'libX*' \
           'libSM*' 'libICE*' 'libuuid*' \
           'libGL*' 'libEGL*' 'libGLX*' 'libGLdispatch*' 'libOpenGL*' 'libglvnd*' \
           'libwayland*' 'libgbm*' 'libdrm*' 'libva*' \
           'libexpat*' 'libbrotli*' 'libbz2*' 'libz.so*' \
           'libmount*' 'libblkid*' 'libselinux*' 'libsystemd*' 'libudev*' 'libcap*' \
           'libffi*' 'libpcre*' 'libicu*' 'libmd4c*' 'libdouble-conversion*'; do
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
echo "bundled libs kept: $(ls "$LIBDIR" | wc -l) ($(ls "$LIBDIR" | tr '\\n' ' '))"

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
exec "$HERE/usr/bin/droppix_client" "$@"
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

# Deliver a dated + short-commit copy into the repo's git-ignored "complete builds/".
DEST="$REPO/complete builds"
mkdir -p "$DEST"
SHA="$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo nogit)"
FINAL="$DEST/droppix-client-$(date +%Y%m%d-%H%M)-$SHA-x86_64.AppImage"
cp "$OUT" "$FINAL"
chmod +x "$FINAL" 2>/dev/null || true
echo "delivered: $FINAL"
