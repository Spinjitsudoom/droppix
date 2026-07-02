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
# Env overrides: DROPPIX_BUILD_DIR (default ~/droppix-build), OUT, APPIMAGETOOL.
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

# --- tooling: linuxdeploy (bundles ldd deps + sets $ORIGIN rpaths) + patchelf ---
LD="$CACHE/linuxdeploy-x86_64.AppImage"
if [ ! -x "$LD" ]; then
  echo "fetching linuxdeploy..."
  curl -fsSL -o "$LD" "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
  chmod +x "$LD"
fi
distrobox enter "$DISTROBOX" -- bash -lc 'command -v patchelf >/dev/null || sudo dnf install -y patchelf'

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
distrobox enter "$DISTROBOX" -- bash -lc "
  export APPIMAGE_EXTRACT_AND_RUN=1 NO_STRIP=1
  cd '$BUILD_DIR'
  '$LD' --appimage-extract-and-run --appdir AppDir \
    -e droppix_gui -e droppix_stream -i droppix.png -d droppix.desktop
"

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
echo "bundled libs kept: $(ls "$LIBDIR" | wc -l)"

# --- package (host: appimagetool needs `file`; use a cached runtime if present) ---
RT="$CACHE/runtime-x86_64"
rm -f "$OUT"
if [ -s "$RT" ]; then
  ARCH=x86_64 "$APPIMAGETOOL" --runtime-file "$RT" "$APPDIR" "$OUT"
else
  ARCH=x86_64 "$APPIMAGETOOL" "$APPDIR" "$OUT"   # appimagetool fetches its runtime
fi
echo "built: $OUT ($(stat -c%s "$OUT") bytes)"
