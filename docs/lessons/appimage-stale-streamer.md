# L-2026-11-24-appimage-stale-streamer: an AppImage could run another build's streamer

- **ID:** `L-2026-11-24-appimage-stale-streamer`
- **Tags:** `gui`, `packaging`, `silent-failure`, `regression`, `high`
- **Date:** 2026-11-24

## Symptom

A streamer fix shipped in a new AppImage had no effect, and nothing in the logs explained
why. The GUI and the root streamer could come from different builds.

## Root cause

The AppImage copies its `droppix_stream` (and bundled libs) to
`~/.local/share/droppix/runtime/` so pkexec/root can run it. `resolveStreamBin()` re-copied
only when the staged copy's mtime was *older* than the bundled one, but `QFile::copy` stamps
the copy with the extraction time, not the build time. So:

- running an older AppImage after a newer one kept the newer streamer;
- a rebuild made within minutes of the last extraction kept the previous streamer.

## Fix

Re-extract whenever the staged binary is not byte-identical to the bundled one (size, then
contents). The streamer is under 1 MB, so the comparison costs nothing at launch.

## Related

The polkit rule has to name the *relocated* path too. `scripts/install-polkit-rule.sh` used
to authorize only `~/droppix-build/droppix_stream`, so AppImage users got a password prompt
on every start (and pkexec matches the canonical path, `/var/home/...` on Fedora Atomic).
