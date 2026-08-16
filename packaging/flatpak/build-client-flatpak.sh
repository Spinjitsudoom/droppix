#!/usr/bin/env bash
# Build + install the droppix CLIENT Flatpak (org.droppix.Client) and drop a single-file
# bundle into "complete builds/". Run on the host.
#
# Uses the KDE runtime (org.kde.Platform/Sdk 6.10 — provides Qt6 incl. QtMultimedia;
# install with `flatpak install flathub org.kde.Platform//6.10 org.kde.Sdk//6.10` if
# missing). Builds a decode-only ffmpeg + the client. Unlike the host Flatpak, the
# sandbox stays ON — the client needs nothing host-privileged, just network + audio.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MANIFEST="$REPO/packaging/flatpak/org.droppix.Client.yml"
WORK="${DROPPIX_CLIENT_FLATPAK_WORK:-$HOME/droppix-client-flatpak-build}"
mkdir -p "$WORK"

# flatpak-builder caches git sources as bare repos; a git safe.bareRepository=explicit
# setting blocks that, so override it just for this process.
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.bareRepository GIT_CONFIG_VALUE_0=all

flatpak-builder --user --force-clean --install \
  --state-dir "$WORK/state" --repo "$WORK/repo" "$WORK/build" "$MANIFEST"

# Deliver a single-file bundle into the repo's "complete builds/" (git-ignored).
DEST="$REPO/complete builds"; mkdir -p "$DEST"
SHA="$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo nogit)"
NAME="droppix-client-$(date +%Y%m%d-%H%M)-$SHA.flatpak"
BUNDLE="$DEST/$NAME"
# build-bundle writes a temp file and renames it into place. "complete builds/" can sit on
# a CIFS mount, where that rename fails with EAGAIN ("Resource temporarily unavailable")
# after the whole build has already succeeded. Build it on local disk, then copy.
STAGE="$WORK/$NAME"
flatpak build-bundle "$WORK/repo" "$STAGE" org.droppix.Client
cp -f "$STAGE" "$BUNDLE"

echo "delivered: $BUNDLE"
echo "installed to --user; run with:  flatpak run org.droppix.Client"
