# G-2026-11-25-release-artifact-folders: the release job tried to upload a folder

- **ID:** `G-2026-11-25-release-artifact-folders`
- **Tags:** `ci`, `release`, `gotcha`, `low`, `lesson`
- **Date:** 2026-11-25

The first CI release (v0.4.0) built every package, then failed to publish with
`read dist/bin: is a directory`. `actions/download-artifact` with `merge-multiple: true` downloads
**every** artifact of the run. That included `portable-host` (the streamer's `bin/` + `lib/` tree
handed from the AppImage job to the Flatpak job), and `gh release create dist/*` choked on its
folders. Nothing had been published.

Fix: the release job downloads only the `appimages` and `flatpaks` artifacts by name, and uploads
only regular files (`find dist -maxdepth 1 -type f`). v0.4.0 itself was published by hand from that
run's artifacts (`gh run download … -n appimages -n flatpaks`) plus the locally signed APK.
