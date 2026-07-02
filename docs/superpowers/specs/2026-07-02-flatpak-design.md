# droppix Flatpak (full-function via host-spawn) — design

**Date:** 2026-07-02
**Status:** built & verified (evdi path needs on-device confirmation)

## Goal

Package droppix as a Flatpak that delivers the *full* feature set (extended monitor,
touch, audio), not a crippled sandboxed subset.

## The core tension

A Flatpak sandbox fundamentally cannot run droppix's core: the root `pkexec` streamer,
`/dev/uinput`, evdi, and host KWin/PipeWire/avahi/adb. So a "sandboxed" Flatpak could only
do GUI + WiFi test-pattern. To get full function, droppix reaches the **host** via
`flatpak-spawn --host`. That requires `--talk-name=org.freedesktop.Flatpak`, which
**effectively disables the sandbox** — the Flatpak becomes a distribution-format
alternative to the AppImage, with no security benefit. (User chose this knowingly.)

## Design

**Runtime:** `org.kde.Platform//6.10` (provides Qt6) + `org.kde.Sdk//6.10` to build.

**Modules built in the manifest** (`org.droppix.Droppix.yml`): x264, ffmpeg
(`--enable-gpl --enable-libx264` — the runtime's ffmpeg has no x264), libevdi (userspace),
then droppix (`cmake`, `-DDROPPIX_BUILD_TESTS=OFF` so the offline build skips the
network-only GoogleTest fetch; `-DCMAKE_PREFIX_PATH=/app` so `find_library(evdi)` sees the
module).

**Host-spawn shims** (`host-shim`, installed on PATH as `pkexec`, `adb`,
`avahi-publish-service`, `avahi-browse`): each `exec flatpak-spawn --host <name> "$@"`, so
the GUI's existing `QProcess("<tool>")` calls transparently run on the host. `openssl`
stays in-sandbox (cert gen + readback; the runtime has it). `kscreen-doctor`/`qdbus`/
`parec`/`runuser` are only invoked by the streamer, which already runs on the host, so they
resolve natively there.

**Streamer on the host** (`MainWindow::resolveStreamBin`, Flatpak branch):
- The manifest tars `droppix_stream` + the `/app` libs it links (ffmpeg/x264/evdi) + a
  wrapper that sets `LD_LIBRARY_PATH` → `/app/share/droppix/droppix-runtime.tar.gz`.
- On launch the GUI queries the host `$HOME` and pipes that tarball to the host over the
  `flatpak-spawn` stdio, extracting to `$HOME/.local/share/droppix/runtime/`. `streamBin_`
  becomes that host wrapper path.
- `build_command` already does `pkexec <streamBin_>` for evdi → the pkexec shim →
  `flatpak-spawn --host pkexec <host wrapper>`; the wrapper sets `LD_LIBRARY_PATH` (pkexec
  strips it from the env, so the wrapper must set it itself) and execs the streamer as root.
- `stageCertsToHost()` mirrors the freshly generated cert/key to the host runtime dir, and
  `collectSettings` hands the streamer those host paths (`--cert`/`--key`).

`finish-args`: `--socket=wayland`/`--socket=fallback-x11`, `--share=ipc`/`--share=network`,
`--device=dri`, `--talk-name=org.freedesktop.Flatpak`, `--filesystem=xdg-config/droppix_gui`
+ `xdg-data/droppix`.

## Verified

- Manifest builds (x264 + ffmpeg + evdi + droppix) against the KDE SDK.
- The Flatpak GUI launches (Qt6 from the runtime); in-sandbox openssl cert-gen works.
- On launch the streamer runtime + cert stage onto the host, and the host-staged
  `droppix_stream_host --test-pattern` runs and opens its libx264 encoder — proving the
  wrapper + bundled ffmpeg/x264/evdi work on the host.

## Not verified here (needs device + evdi module)

The full evdi/root path end-to-end: `pkexec` (auth prompt) → root streamer → evdi virtual
output → tablet. The plumbing is complete; a live device run is the remaining confirmation.

## Notes / limits

- The test-pattern *Source* won't run from the Flatpak GUI (that path invokes the streamer
  in-sandbox, not via host-spawn); evdi is the intended path and goes through pkexec.
- `setupAuth` ("remember auth permanently") writes a polkit rule via `pkexec install <tmp>`;
  the tmp file is a sandbox path the host `install` can't read, so this is a known gap in
  the Flatpak (streaming still prompts for the password each session unless configured on
  the host).

## Out of scope

- Publishing to Flathub (the org.freedesktop.Flatpak permission would not pass review).
