# Build-time version from `git describe` without recompiling the GUI

**ID:** G-2026-08-06-build-time-version · **Tags:** gui, packaging, gotcha · **Severity:** low

## Context

The GUI showed a hardcoded `Version 0.1.0`, so successive builds were indistinguishable
even when behavior changed. We wanted the running build to be identifiable at a glance.

## What works

Resolve the version at **build time** from `git describe --tags --always --dirty`
(`host/cmake/GenerateVersion.cmake`, run via `cmake -P` from a `add_custom_target`,
not at configure time — configure-time only updates when CMake reruns, so a plain
`make` after new commits would show a stale SHA).

Three constraints that make it cheap and robust:

1. **Write-if-changed.** The generator only rewrites `droppix_version.h` when the string
   actually changes. A stable checkout doesn't rewrite it, so dependents don't recompile
   every build despite the always-run custom target.
2. **One-TU accessor.** `version.h` declares `app_version()` and contains **no** version
   string; only `version.cpp` includes the generated header. So a version change relinks
   `version.cpp` alone, not big consumers like `main_window.cpp`. If you `#include` the
   generated header from a widely-included header, every build recompiles half the GUI.
3. **Seed the header at configure time.** `droppix_gui` doesn't link `droppix_core`, so
   the generated include dir + `add_dependencies(<target> droppix_version_gen)` must be
   added to each target that compiles `version.cpp` (core, gui, gui_tests). CMake also
   scans sources at configure time before the custom target runs, so `CMakeLists.txt`
   pre-creates a fallback `droppix_version.h` if it's missing.

## Gotchas

- `git describe`'s working dir is the **source** tree, not the build dir. Our build dir is
  off-mount (distrobox), but git resolves fine because `WORKING_DIRECTORY` points at the
  mounted source. Verified `git describe` works from inside the distrobox before relying
  on it; if git is unavailable or the tree isn't a repo, the script falls back to `0.1.0`.
- Tests must assert the label equals `"Version " + app_version()`, not a hardcoded
  `"0.1.0"` substring — the substring check silently passed on both the real describe and
  the fallback, hiding whether the dynamic value was actually wired.

## Where

`host/cmake/GenerateVersion.cmake`, `host/src/version.{h,cpp}`, `host/CMakeLists.txt`,
`host/gui/main_window.cpp` (title bar + About dialog), `host/gui/pages/about_page.cpp`,
`host/tests/test_version.cpp`, `host/gui/tests/test_about_page.cpp`.
