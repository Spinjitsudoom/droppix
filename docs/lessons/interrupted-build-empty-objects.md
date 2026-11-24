# G-2026-11-24-interrupted-build-empty-objects: a container restart mid-build left 0-byte objects, and tests silently vanished

- **ID:** `G-2026-11-24-interrupted-build-empty-objects`
- **Tags:** `packaging`, `silent-failure`, `gotcha`, `medium`
- **Date:** 2026-11-24

## Symptom

`ctest` reported "100% passed" for 75 tests, then 367, when the suite has 380+.
`droppix_tests` printed nothing and exited 0. `droppix_gui` failed to link with
"undefined reference to vtable".

## Root cause

The `droppix-dev` distrobox restarted ("Container Setup Complete") while a build was
running. Compilers and linkers died mid-write and left **0-byte** `.o` files and binaries
with fresh mtimes, so `make` treated them as up to date. A 0-byte test binary yields zero
discovered tests: ctest reports success for what is left. A test binary linked from empty
objects just has fewer tests.

## Fix

```bash
cd ~/droppix-build
find . -type f -size 0 \( -name '*.o' -o -perm -u+x \) -newermt "<time of the restart>" -delete
```

Then rebuild. Clear `droppix_gui_autogen` too if moc output looks stale.

## How to detect this in the future

Always compare the ctest total with the last known count (`docs/STATUS.md`, **Last
verified**). A drop with no failures means tests were lost, not passed.
