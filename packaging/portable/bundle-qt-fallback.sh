#!/usr/bin/env bash
# Copy the build container's Qt 6 (libs + the plugins droppix_gui needs) and the Qt-specific
# libraries a Qt-less host may lack into DEST/{lib,plugins}. Run inside the old-base distrobox.
# Deliberately NOT copied (the host always has them, and bundling them is what crashed
# before): glibc, libstdc++, glib/gio, libX11/libxcb core, GL/EGL, wayland, fontconfig,
# freetype, harfbuzz, zlib, libdrm, dbus.
set -euo pipefail
DEST="$1"
QTLIB="$(dirname "$(readlink -f "$(ldconfig -p | awk '/libQt6Core.so.6 /{print $NF; exit}')")")"
QTPLUG="$(qtpaths6 --plugin-dir 2>/dev/null || echo /usr/lib/x86_64-linux-gnu/qt6/plugins)"
rm -rf "$DEST"; mkdir -p "$DEST/lib" "$DEST/plugins"
for d in wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client \
         xcbglintegrations imageformats iconengines tls; do
  [ -d "$QTPLUG/$d" ] && cp -a "$QTPLUG/$d" "$DEST/plugins/"
done
# Desktop platform plugins only: no eglfs/linuxfb/vnc (they drag in libinput, libwacom, ...)
# and no platformthemes (the GTK3 theme drags in all of GTK/cairo/pango).
mkdir -p "$DEST/plugins/platforms"
for p in libqxcb.so libqwayland-generic.so libqwayland-egl.so libqoffscreen.so libqminimal.so; do
  [ -f "$QTPLUG/platforms/$p" ] && cp -a "$QTPLUG/platforms/$p" "$DEST/plugins/platforms/"
done
# Qt libraries themselves (not the embedded-display ones)
for l in "$QTLIB"/libQt6*.so.6; do
  case "$(basename "$l")" in libQt6EglFs*|libQt6Quick*|libQt6Qml*|libQt6Designer*) continue ;; esac
  cp -L "$l" "$DEST/lib/"
done
# Every dependency of Qt + its plugins EXCEPT what any desktop host has (and what crashed when
# bundled: glib/xcb core/GL). Older copies of the rest are safe: this folder is only on the
# library path when the host has no Qt 6 at all.
HOST_HAS='^(libc|libm|libdl|libpthread|librt|libresolv|libutil|ld-linux[^/]*|libstdc\+\+|libgcc_s|libGL|libGLX|libGLdispatch|libOpenGL|libEGL|libgbm|libdrm|libX11|libX11-xcb|libXau|libXdmcp|libXext|libXrender|libXi|libxcb|libxkbcommon|libglib-2\.0|libgobject-2\.0|libgio-2\.0|libgmodule-2\.0|libgthread-2\.0|libffi|libpcre2-8|libpcre|libmount|libblkid|libselinux|libcap|libz|libexpat|libpng16|libbz2|libbrotli[a-z]*|libfontconfig|libfreetype|libharfbuzz|libgraphite2|libdbus-1|libsystemd|libudev|libwayland-[a-z]*|libssl|libcrypto|liblzma|libzstd|liblz4|libgcrypt|libgpg-error|libuuid)\.so'
{ ls "$DEST"/lib/libQt6*.so.6; find "$DEST/plugins" -name '*.so'; } | while read -r f; do
  ldd "$f" 2>/dev/null | awk '/=> \//{print $3}'
done | sort -u | while read -r dep; do
  b="$(basename "$dep")"
  case "$b" in libQt6*) continue ;; esac
  echo "$b" | grep -Eq "$HOST_HAS" && continue
  cp -L "$dep" "$DEST/lib/"
done
# libOpenGL.so.0 (glvnd's thin dispatcher) is missing on some Qt-less installs; it lives apart
# and AppRun adds it only when the host has none. It forwards to the host's GL vendor driver.
mkdir -p "$DEST/gl"
cp -L "$(ldconfig -p | awk '/libOpenGL.so.0 /{print $NF; exit}')" "$DEST/gl/"
# Plugins find their Qt libs via LD_LIBRARY_PATH (set by AppRun); drop build-time rpaths.
find "$DEST" -name '*.so*' -exec patchelf --remove-rpath {} \; 2>/dev/null || true
echo "qt fallback: $(ls "$DEST/lib" | wc -l) libs, $(find "$DEST/plugins" -name '*.so' | wc -l) plugins"
