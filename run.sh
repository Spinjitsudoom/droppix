#!/usr/bin/env bash
#
# run.sh — build droppix from the CURRENT source and launch the GUI.
#
# The repo sits on a no-exec CIFS mount, so:
#   * the build runs inside the `droppix-dev` distrobox with an off-mount build dir, and
#   * the freshly built droppix_gui is launched inside that same container (it has the
#     runtime libs; the host Wayland/X sockets are forwarded automatically).
#
# Because the mount is no-exec, invoke it as:   bash run.sh   (not ./run.sh)
#
# Usage:
#   bash run.sh               # incremental build of the latest source, then launch the GUI
#   bash run.sh --no-build    # launch the existing build without rebuilding
#
# Env overrides:  DROPPIX_BOX   (distrobox name, default droppix-dev)
#                 DROPPIX_BUILD (build dir,      default $HOME/droppix-build)

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOX="${DROPPIX_BOX:-droppix-dev}"
BUILD="${DROPPIX_BUILD:-$HOME/droppix-build}"

if [[ "${1:-}" != "--no-build" ]]; then
  echo ">> building droppix (latest source) in '$BOX' ..."
  distrobox enter "$BOX" -- bash -lc \
    "cmake -S \"$REPO/host\" -B \"$BUILD\" >/dev/null && cmake --build \"$BUILD\" --target droppix_gui droppix_stream -j"
fi

echo ">> launching droppix_gui  (close the window to exit) ..."
exec distrobox enter "$BOX" -- "$BUILD/droppix_gui"
