#!/usr/bin/env bash
# Prepare the "old base" build environment for portable droppix packages.
#
# Why: binaries built on a new distro need that distro's glibc (the AppImage built on
# Fedora 44 needed glibc 2.43 and Qt 6.11, so it ran almost nowhere else). Building on
# Ubuntu 22.04 (glibc 2.35, Qt 6.2) gives binaries that run on Ubuntu 22.04+, Debian 12+,
# Fedora, Arch, openSUSE, ... with the host's (newer, ABI-compatible) Qt and system libs.
#
# Run INSIDE an ubuntu:22.04 distrobox (see packaging/portable/README.md):
#   distrobox create --name droppix-oldbase --image docker.io/library/ubuntu:22.04
#   distrobox enter droppix-oldbase -- bash packaging/portable/setup-oldbase.sh
#
# Installs apt build deps, then builds into $PREFIX (default /opt/droppix-deps):
#   x264, nv-codec-headers, FFmpeg (avcodec/avutil/swscale only), libevdi.
# Re-running skips anything already installed.
set -euo pipefail

PREFIX="${PREFIX:-/opt/droppix-deps}"
SRC="${SRC:-$HOME/.cache/droppix-oldbase-src}"
JOBS="$(nproc)"
FFMPEG_VER=7.1
EVDI_VER=v1.14.7
mkdir -p "$SRC"

if ! command -v cmake >/dev/null || ! dpkg -s qt6-base-dev qt6-multimedia-dev >/dev/null 2>&1; then
  sudo apt-get update -q
  sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q \
    build-essential cmake pkg-config git curl ca-certificates nasm file patchelf \
    qt6-base-dev qt6-base-dev-tools libqt6opengl6-dev libgl-dev libglx-dev \
    qt6-wayland libqt6svg6 qt6-multimedia-dev \
    libssl-dev libusb-1.0-0-dev libva-dev libdrm-dev libgtest-dev
fi
sudo mkdir -p "$PREFIX" && sudo chown "$(id -u):$(id -g)" "$PREFIX"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig:${PKG_CONFIG_PATH:-}"

# --- x264 (shared) ---
if [ ! -f "$PREFIX/lib/libx264.so" ]; then
  [ -d "$SRC/x264" ] || git clone --depth 1 https://code.videolan.org/videolan/x264.git "$SRC/x264"
  (cd "$SRC/x264" && ./configure --prefix="$PREFIX" --enable-shared --disable-cli --enable-pic &&
   make -j"$JOBS" && make install)
fi

# --- NVENC headers (header-only; libcuda/libnvidia-encode are dlopen'd on the host) ---
if [ ! -f "$PREFIX/include/ffnvcodec/nvEncodeAPI.h" ]; then
  [ -d "$SRC/nv-codec-headers" ] || git clone --depth 1 https://github.com/FFmpeg/nv-codec-headers.git "$SRC/nv-codec-headers"
  make -C "$SRC/nv-codec-headers" PREFIX="$PREFIX" install
fi

# --- FFmpeg: only what droppix links (avcodec, avutil, swscale) + its encoders ---
if [ ! -f "$PREFIX/lib/libavcodec.so" ]; then
  T="$SRC/ffmpeg-$FFMPEG_VER.tar.xz"
  [ -f "$T" ] || curl -fsSL -o "$T" "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VER.tar.xz"
  rm -rf "$SRC/ffmpeg-$FFMPEG_VER" && tar -xf "$T" -C "$SRC"
  (cd "$SRC/ffmpeg-$FFMPEG_VER" &&
   ./configure --prefix="$PREFIX" --enable-shared --disable-static --enable-gpl \
     --disable-programs --disable-doc --disable-avdevice --disable-avformat \
     --disable-avfilter --disable-swresample --disable-postproc --disable-network \
     --enable-libx264 --enable-vaapi --enable-nvenc --enable-ffnvcodec \
     --extra-cflags="-I$PREFIX/include" --extra-ldflags="-L$PREFIX/lib" &&
   make -j"$JOBS" && make install)
fi

# --- libevdi (userspace library; the kernel module stays the host's) ---
if [ ! -f "$PREFIX/lib/libevdi.so" ]; then
  [ -d "$SRC/evdi" ] || git clone --depth 1 --branch "$EVDI_VER" https://github.com/DisplayLink/evdi.git "$SRC/evdi"
  make -C "$SRC/evdi/library" -j"$JOBS"
  install -Dm755 "$SRC/evdi/library/libevdi.so" "$PREFIX/lib/libevdi.so.1"
  ln -sf libevdi.so.1 "$PREFIX/lib/libevdi.so"
  install -Dm644 "$SRC/evdi/library/evdi_lib.h" "$PREFIX/include/evdi_lib.h"
fi

echo "old-base deps ready in $PREFIX"
