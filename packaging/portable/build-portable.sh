#!/usr/bin/env bash
# Build the PORTABLE droppix AppImage: compiled on Ubuntu 22.04 (glibc 2.35, Qt 6.2) so it
# runs on Ubuntu 22.04+, Debian 12+, current Fedora/Arch/openSUSE, ... and bundles a Qt
# fallback for hosts with no Qt 6. Run on the HOST (appimagetool needs `file`):
#
#   bash packaging/portable/build-portable.sh
#
# One-time: distrobox create --name droppix-oldbase --image docker.io/library/ubuntu:22.04
# (this script runs setup-oldbase.sh inside it, which is a no-op once the deps exist).
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BOX="${DROPPIX_DISTROBOX:-droppix-oldbase}"
BUILD="${DROPPIX_BUILD_DIR:-$HOME/droppix-build-portable}"
DEPS=/opt/droppix-deps

# DROPPIX_DISTROBOX=none: already on an Ubuntu 22.04 machine (CI runner); run directly.
inbox() { if [ "$BOX" = none ]; then bash -lc "$1"; else distrobox enter "$BOX" -- bash -lc "$1"; fi; }
inbox "bash '$REPO/packaging/portable/setup-oldbase.sh'"
inbox "
  set -e
  export PKG_CONFIG_PATH=$DEPS/lib/pkgconfig LD_LIBRARY_PATH=$DEPS/lib
  cmake -S '$REPO/host' -B '$BUILD' -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$DEPS \
        -DCMAKE_BUILD_RPATH=$DEPS/lib >/dev/null
  cmake --build '$BUILD' -j\$(nproc)
  QT_QPA_PLATFORM=offscreen ctest --test-dir '$BUILD' -j\$(nproc) --output-on-failure 2>&1 | tail -3
  [ \${PIPESTATUS[0]} -eq 0 ]
"
# Only what the binaries need that a host may lack (or have at the wrong ABI): FFmpeg, x264,
# libevdi. OpenSSL 3, libusb, libva, libdrm and X11 come from the host.
DROPPIX_DISTROBOX="$BOX" DROPPIX_BUILD_DIR="$BUILD" DROPPIX_DEPS_LIB="$DEPS/lib" QT_FALLBACK=1 \
  KEEP_LIBS="libavcodec libavutil libswscale libx264 libevdi" \
  bash "$REPO/packaging/appimage/build-appimage.sh"

# The desktop client (client/): same base, same portable packaging.
if [ "${DROPPIX_WITH_CLIENT:-0}" = 1 ]; then
  CBUILD="${DROPPIX_CLIENT_BUILD_DIR:-$HOME/droppix-client-build-portable}"
  inbox "
    set -e
    export PKG_CONFIG_PATH=$DEPS/lib/pkgconfig LD_LIBRARY_PATH=$DEPS/lib
    cmake -S '$REPO/client' -B '$CBUILD' -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$DEPS >/dev/null
    cmake --build '$CBUILD' -j\$(nproc)
  "
  DROPPIX_DISTROBOX="$BOX" DROPPIX_CLIENT_BUILD_DIR="$CBUILD" DROPPIX_DEPS_LIB="$DEPS/lib" QT_FALLBACK=1 \
    KEEP_LIBS="libavcodec libavutil libswscale libx264" \
    bash "$REPO/packaging/appimage/build-client-appimage.sh"
fi
