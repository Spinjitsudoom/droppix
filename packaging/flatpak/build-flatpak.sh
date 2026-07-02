#!/usr/bin/env bash
# Build + install the droppix Flatpak (org.droppix.Droppix) and drop a single-file
# bundle into "complete builds/". Run on the host.
#
# The Flatpak uses the KDE runtime (org.kde.Platform/Sdk 6.10 — install with
# `flatpak install flathub org.kde.Platform//6.10 org.kde.Sdk//6.10` if missing), which
# provides Qt6. It builds x264 + ffmpeg(--enable-libx264) + libevdi + droppix as modules.
#
# droppix can't run its root/evdi streamer or reach host KWin/polkit/PipeWire/adb from
# inside the sandbox, so it reaches the host via `flatpak-spawn --host` (PATH shims +
# staging the streamer onto the host). That needs --talk-name=org.freedesktop.Flatpak,
# which effectively disables the sandbox. The AppImage delivers the same capability; the
# Flatpak is a distribution-format alternative. See docs/.../2026-07-02-flatpak-design.md.
#
# Host still needs (not bundleable): the evdi kernel module (DKMS), KDE Plasma, PipeWire,
# avahi, adb, polkit — same as the AppImage.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MANIFEST="$REPO/packaging/flatpak/org.droppix.Droppix.yml"
WORK="${DROPPIX_FLATPAK_WORK:-$HOME/droppix-flatpak-build}"
mkdir -p "$WORK"

# flatpak-builder caches git sources as bare repos; a git safe.bareRepository=explicit
# setting blocks that, so override it just for this process.
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.bareRepository GIT_CONFIG_VALUE_0=all

flatpak-builder --user --force-clean --install \
  --state-dir "$WORK/state" --repo "$WORK/repo" "$WORK/build" "$MANIFEST"

# Deliver a single-file bundle into the repo's "complete builds/" (git-ignored).
DEST="$REPO/complete builds"; mkdir -p "$DEST"
SHA="$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo nogit)"
BUNDLE="$DEST/droppix-$(date +%Y%m%d-%H%M)-$SHA.flatpak"
flatpak build-bundle "$WORK/repo" "$BUNDLE" org.droppix.Droppix

echo "delivered: $BUNDLE"
echo "installed to --user; run with:  flatpak run org.droppix.Droppix"
