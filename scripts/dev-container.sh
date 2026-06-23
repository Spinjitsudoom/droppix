#!/usr/bin/env bash
# Create and enter the droppix-dev Fedora distrobox.
# The container shares the host kernel's evdi module and /dev nodes.
set -euo pipefail

NAME=droppix-dev
IMAGE=registry.fedoraproject.org/fedora:44

if ! distrobox list | grep -q "\b${NAME}\b"; then
  distrobox create --name "${NAME}" --image "${IMAGE}" --yes
fi

# Install build dependencies inside the container (idempotent).
# libevdi is not in the stock Fedora repos; it ships from the negativo17
# "multimedia" repo (the same source used on the Bazzite host). Enable it
# first so dnf can resolve the package.
distrobox enter "${NAME}" -- bash -lc '
  sudo dnf install -y dnf-plugins-core
  sudo dnf config-manager addrepo --from-repofile=https://negativo17.org/repos/fedora-multimedia.repo 2>/dev/null || true
  sudo dnf config-manager setopt fedora-multimedia.enabled=1 2>/dev/null || true
  sudo dnf install -y gcc-c++ cmake git libevdi kscreen ffmpeg ffmpeg-devel x264-devel 2>/dev/null || \
  sudo dnf install -y gcc-c++ cmake git libevdi ffmpeg ffmpeg-devel x264-devel
'

echo "Container ready. Enter it with: distrobox enter ${NAME}"
