# evdi install helper — design

**Status:** Designed (2026-11-25). Not started.

## Intent

droppix should just work on whatever Linux system it lands on. The user asked for
droppix to "switch to the necessary module (evdi or equivalent) depending on the OS",
and the reason given is **ease of use**.

Today the evdi kernel module is a hard requirement that fails silently: if it isn't
installed, the streamer falls back to a test pattern and the only clue is a log line.
The userspace side is already handled. The AppImage and Flatpak bundle `libevdi`,
FFmpeg and x264, and the streamer runs `modprobe evdi` itself when the module is
installed but not loaded (`host/src/virtual_display.cpp`, `ensure_evdi_module`). The
one thing droppix still needs from the system is the kernel module itself.

## The larger plan (two projects, this is the first)

1. **This design: the evdi install helper.** droppix notices that evdi is missing and
   installs it, or tells the user exactly how, per distro.
2. **Later, separate design: built-in virtual monitors.** GNOME (Mutter), Plasma 6
   (KWin), Sway and Hyprland can create a virtual output themselves, with no kernel
   module, and hand the picture over PipeWire. These would plug in as new `FrameSource`
   implementations next to `EvdiFrameSource`, one desktop at a time, each replacing evdi
   on its desktop only once it matches evdi in testing. That work starts with a latency
   spike on GNOME. X11 and Plasma 5 keep evdi permanently.

The helper ships first because it helps every distro immediately and stays useful
after project 2 (X11, Plasma 5 and unsupported desktops will always need evdi).

## Goals

- A user with no evdi sees, without opening logs, that one system component is missing,
  and gets a one-click install on the distros where that can be done safely.
- Where droppix can't install for them, they get the exact commands for their system,
  copy-able, plus why.
- Secure Boot is detected and explained **before** the install, and the key-enrollment
  step is handled as far as software can.
- After an install, droppix loads the module and verifies it, and reports a real reason
  if it still fails.
- The kernel module installed by droppix matches the `libevdi` version droppix bundles.

## Non-goals

- Built-in virtual monitors (project 2 above).
- Uninstalling or upgrading an evdi that the distro or the user already installed.
- Making the streamer run without root.
- Non-x86_64 architectures, Windows, macOS.

## Anti-goals (would be a failure even if it technically works)

- Enabling a third-party repository, or replacing any system package other than the
  evdi module and its build dependencies (headers, compiler, `dkms`). In particular,
  never enable negativo17 `fedora-multimedia`: it replaces the system FFmpeg for every app.
- Changing the system without a visible confirmation that names what will be installed.
- Leaving a half-installed state with no explanation (e.g. a DKMS module that builds but
  the kernel refuses to load, with the UI saying "done").
- Downloading code at install time. Everything the install builds ships inside droppix.

## Constraints

- The GUI runs unprivileged. Root work goes through `pkexec`, as the existing polkit
  setup does (`MainWindow::setupAuth`). The existing polkit rule only pre-authorizes the
  streamer binary, so the install will prompt for a password; that is expected.
- **Flatpak:** the GUI is sandboxed and reaches the host through `flatpak-spawn --host`
  (`hostSpawnCapture`, the `pkexec` shim). Distro detection must read the **host's**
  `/run/host/os-release`, not the runtime's `/etc/os-release`. Files root must read go to
  the host-staged runtime dir, as the streamer already does.
- **AppImage:** root must read anything it runs from `~/.local/share/droppix/runtime/`
  (already root-readable, already used for the streamer and web assets).
- Fedora Atomic / Bazzite (`/run/ostree-booted` exists): `/usr` is read-only and DKMS
  can't build in place. No one-click install there.
- AUR helpers refuse to run as root. No one-click install on Arch-based distros.
- Secure Boot: an unsigned module is refused by the kernel. DKMS signs with its own key
  (`/var/lib/dkms/mok.pub` on current DKMS), which must be enrolled once via `mokutil
  --import` plus a one-time password typed into the firmware's MOK screen on next boot.
  That firmware step cannot be automated.
- The bundled `libevdi` is pinned at `v1.14.7` (`packaging/portable/setup-oldbase.sh`
  `EVDI_VER`, and the Flatpak `libevdi` module). That release is unlikely to build against
  7.x kernels; upstream's latest is `v1.15.1` (2026-09-15).

## Approaches considered

**For the larger goal (chosen in conversation: C).**

- **A. Install helper only.** Small, helps everyone now, keeps one tested video path.
  Users still install a kernel module and still face Secure Boot enrollment.
- **B. Built-in virtual monitors wherever the desktop has them, evdi elsewhere.**
  Biggest win (most modern-desktop users install nothing, Secure Boot stops mattering),
  but large: a PipeWire capture path, per-compositor output creation, full re-test of
  every desktop, unknown latency.
- **C. A first, then B desktop by desktop.** Chosen. Gets a real improvement out now
  without betting everything on the rewrite.

**For Fedora Workstation (user left the call to Claude; Claude chose the second).**

- *Enable negativo17 `fedora-multimedia` and install `akmod-evdi`.* Rejected: swaps the
  system FFmpeg and other media packages for every app (anti-goal).
- *Build the pinned evdi source with Fedora's own `dkms`.* **Chosen.** Only Fedora's own
  repos, nothing else on the system changes, the module matches our bundled `libevdi`,
  and DKMS rebuilds it on kernel updates. Doubles as the fallback for unknown distros.
- *Only show commands.* Kept for the cases where the chosen path can't run.

## Design

### 1. Detection (unprivileged, runs at GUI startup and on "Check again")

A pure function computes an `EvdiState` from facts gathered on the host:

| State | How it is detected |
|---|---|
| `Loaded` | `/sys/devices/evdi` exists |
| `Installed` | not loaded, but `modinfo evdi` succeeds (the streamer's `modprobe` will load it) |
| `Missing` | neither |

`Installed` is treated as fine: no banner. A module that is installed but refused by the
kernel (Secure Boot) can only be found by trying to load it, which needs root. So the
streamer reports that case: when its `modprobe evdi` fails, it logs the exit status and
`modprobe`'s stderr, and the GUI's log classifier turns a "Key was rejected" /
"Required key not available" line into the same banner, in its Secure Boot form.

In Flatpak, `modinfo` and the `/sys` check run through `flatpak-spawn --host`.

### 2. System facts (pure, unit-tested parsers)

- **Distro family**, from `os-release` `ID` and `ID_LIKE`: `debian` (Debian, Ubuntu,
  Mint, Pop!_OS, … anything with `debian` or `ubuntu` in `ID_LIKE`), `fedora`, `suse`,
  `arch`, `unknown`. `/run/ostree-booted` overrides to `atomic` regardless of ID.
- **Secure Boot**: `mokutil --sb-state` when present; otherwise the last byte of
  `/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d0-aa0d-00e098032b8c`; no EFI dir
  means legacy boot, i.e. off.
- **Kernel release**: `uname -r`, for the headers package.

### 3. What the helper offers, per family

Every one-click path is the same: the distro's **own** repos supply only the build tools
(`dkms`, headers for the running kernel, a compiler), and droppix builds its bundled evdi
source. The distro's own evdi package is deliberately not used, so the module always
matches the bundled `libevdi`, and there is one install path to test instead of four.

| Family | Offer |
|---|---|
| `debian` | **One-click.** `apt-get install -y dkms linux-headers-$(uname -r) build-essential`, then the bundled-source DKMS build. |
| `fedora` | **One-click.** `dnf install -y dkms kernel-devel-$(uname -r) gcc make`, then the bundled-source DKMS build. |
| `suse` | **One-click.** `zypper install -y dkms kernel-default-devel gcc make` (builder verifies names and the matching-kernel headers package), then the bundled-source DKMS build. |
| `arch` | **Commands only.** The AUR command (builder verifies the current package name) plus `linux-headers`, with a note that AUR helpers must run as the user, not root. |
| `atomic` | **Commands only.** Explain that this system can't build kernel modules in place; point to the image's own options (many uBlue images such as Bazzite ship evdi already; otherwise layering a kmod package with `rpm-ostree` and rebooting). No repo enabling suggested. |
| `unknown` | **One-click** if `dkms` exists on the host (bundled-source DKMS build); otherwise a short explanation with a link to the evdi project's install notes. |

**Bundled-source DKMS build.** The evdi module source at the pinned tag ships inside
droppix (AppImage and Flatpak; it is small). The install copies it to
`/usr/src/evdi-<ver>/`, then runs `dkms add`, `dkms build`, `dkms install` for
`evdi/<ver>`, then `modprobe evdi`. If an `evdi` DKMS module of another version is
already registered, the helper does not touch it and shows the existing state instead.

### 4. Privileged step

One `pkexec` call per install runs a droppix-shipped shell script
(`packaging/…/install-evdi.sh`, staged into the root-readable runtime dir, or piped over
stdin in Flatpak as `setupAuth` does) with the chosen plan as arguments. The script:

- prints one progress line per step (`STEP <name>`) and passes tool output through, so the
  dialog can show progress and the F12 log console captures everything;
- stops at the first failing step and exits with a step-specific code;
- ends with `modprobe evdi` and reports whether `/sys/devices/evdi` now exists.

The GUI decides *what* to run (pure, tested plan builder); the script only executes a
fixed set of known steps, never arbitrary command strings from the GUI.

### 5. Secure Boot

- **Before install:** if Secure Boot is on, the confirm dialog says so in plain words:
  the module has to be signed, the PC will show a blue "MOK management" screen on the
  next restart, and the user picks "Enroll MOK" and types a one-time password they set
  now.
- **During install:** if Secure Boot is on, the dialog asks for that one-time password
  (twice), and the script runs `mokutil --import <dkms key>` with it after the build,
  skipping the import if the key is already enrolled (`mokutil --test-key`).
- **After install:** if the module still won't load and Secure Boot is on, the result
  says "restart and enroll the key" instead of "failed".

### 6. UI

- **Status page banner** (only when `Missing`, or the streamer reported a Secure Boot
  refusal): one sentence on what is missing and what it's for, a primary button
  ("Install" or "Show me how"), and "Check again".

  ```
  ┌──────────────────────────────────────────────────────────────┐
  │ ⚠ The extended screen needs the evdi kernel module.          │
  │   Without it, tablets only show a test pattern.              │
  │                              [ Check again ]  [ Install… ]   │
  └──────────────────────────────────────────────────────────────┘
  ```

- **Install dialog:** names exactly what will be installed (packages + "evdi <ver> built
  from source"), the Secure Boot paragraph when relevant, then a progress view with the
  current step and a collapsible log, then the result. "Commands only" families show the
  commands in a read-only text box with a Copy button instead of Install.
- When the streamer is started while evdi is `Missing`, the existing test-pattern
  fallback stays, but the banner is already visible.

### 7. Version pinning

A single source of truth for the evdi version (e.g. `packaging/evdi-version`) is read by
the portable build, the Flatpak manifest and the source-bundling step, so `libevdi` and
the module shipped for DKMS can't drift. As part of this work the pin moves from
`v1.14.7` to the newest upstream release that builds on current kernels (`v1.15.1` at
the time of writing), and the streamer is re-verified against it.

## Testing

- Unit tests (host GUI test suite): `os-release` → family (including `ID_LIKE` chains,
  quotes, Atomic override), Secure Boot parsing (mokutil output and efivar bytes),
  `EvdiState` from facts, the plan builder's output per family, and the log classifier
  line for Secure Boot refusals.
- Script: run in containers for the steps that don't need a real kernel (argument
  handling, step ordering, exit codes).
- End to end in VMs: Ubuntu 24.04 with evdi removed (one-click path, Secure Boot off
  and on with OVMF secure-boot firmware), a Fedora Workstation VM (DKMS path; no Fedora
  VM exists yet in `~/droppix-vms/`), the Arch VM (commands shown, and following them
  works), and the user's Fedora Atomic machine (commands-only path; evdi already present
  there, so it should show no banner).

## Docs to update when shipping

`README.md` requirements (evdi no longer a manual prerequisite on supported distros),
`docs/STATUS.md` row, `docs/ARCHITECTURE.md` if the install script becomes a new
privileged component, this spec's Status line, and a lesson for any non-obvious distro
quirk found on the way.

## Left to the builder

- Exact package names per distro (build tools and headers; the Arch AUR name), verified
  against the live repos, including what to do when headers for the *running* kernel are
  no longer in the repo (the user updated but hasn't rebooted): say "restart first".
- Where each distro's DKMS keeps its signing key (`/var/lib/dkms/mok.pub` on DKMS 3.x;
  Ubuntu's `shim-signed` uses `/var/lib/shim-signed/mok/`), and whether that key already
  exists or DKMS must generate it on first build.
- Where the install script and bundled source live in the tree, and how they are staged.
- The Secure Boot password UI details and how the password reaches `mokutil` without
  touching disk.
- Exit-code table between script and GUI.
- Whether the banner also appears on the Connections page.

## Calls Claude made (not discussed with the user)

- Bundling the evdi source instead of downloading it at install time.
- One install path everywhere (distro build tools + droppix's bundled evdi source via
  DKMS) instead of the distro's own evdi packages.
- Commands-only on Arch and Atomic.
- Handling the `mokutil` import inside the helper rather than just explaining it.
- Bumping the pinned evdi version as part of this work.
