# macOS backend (archived)

Native macOS display + capture code for droppix. **This is archived source — it
is not part of any build.** The maintained, shipping program is the Linux host
under [`../host`](../host), which is Linux-only by design. These files live here
so the macOS work isn't lost and can be picked up later, without dragging
platform conditionals back into the Linux build.

## Related research

Open-source `CGVirtualDisplay` ecosystem survey and recommended ownership model (own thin wrapper; DeskPad / VirtualDisplayKit / daylight-mirror as cribs; no BetterDisplay / DisplayLink dependency):

[`docs/superpowers/specs/2026-07-18-cgvirtualdisplay-oss-research.md`](../docs/superpowers/specs/2026-07-18-cgvirtualdisplay-oss-research.md)

## Status

Incomplete work-in-progress.

- **Phase A — display + capture: implemented here.** A real second monitor via
  the private `CGVirtualDisplay` API, captured with `CGDisplayStream`.
- **Phase B — input injection: NOT implemented.** macOS touch injection
  (`CGEventPost`) was never written; on the original branch the input handler
  was stubbed out with a "not yet implemented on macOS" log.

This code was branched from commit `74c944e` (`wip/macos-port`), **before** the
TLS/PIN pairing, WiFi discovery, and USB-connect work landed on `master`. It has
not been updated to match the current wire protocol or transport. Treat it as a
starting point, not a drop-in.

## Files

| File | Role |
|------|------|
| `src/macos_virtual_display.{h,mm}` | `MacVirtualDisplay` — wraps private `CGVirtualDisplay` to create a real second display (System Settings/Mission Control see it). 60 Hz cap is an API limit. |
| `src/macos_frame_source.{h,mm}` | `MacFrameSource : FrameSource` — captures the virtual display via `CGDisplayStream`; `next()` blocks on a condvar the capture callback signals (mirrors `EvdiFrameSource`). |
| `tests/test_macos_{virtual_display,frame_source}.mm` | Unit tests for the two classes. |

The `.mm` files `#include "frame_source.h"` and other headers from `../host/src`
by their original relative location (they used to live in `host/src`). Paths
assume re-integration into the host tree, not compilation from this folder.

## Re-integrating later

The macOS backend plugs into the shared host code through these seams (full
plumbing is recoverable from `git show wip/macos-port:<path>`):

- **`FrameSource` hook:** `host/src/frame_source.h` gained
  `virtual int native_display_id() const { return -1; }`. evdi/test-pattern
  return -1 (fall back to platform output discovery); `MacFrameSource` returns
  its `CGDirectDisplayID` so the host can target the right display.
- **CMake (`host/CMakeLists.txt`):** under `if(APPLE)` the project enables
  `OBJCXX`, compiles the two `.mm` files with `-fobjc-arc` (ARC is required —
  the `void*`-bridging casts hand ObjC-owned objects across the C++ boundary and
  rely on retain/release), and links `-framework CoreGraphics` (+ Cocoa). The
  evdi source is guarded with `if(NOT APPLE)`.
- **Source selection (`host/src/stream_main.cpp`):** `#include
  "macos_frame_source.h"` and a `--macos` path that constructs `MacFrameSource`
  instead of the evdi source.
- **Input (`host/src/stream_daemon.cpp`):** `#ifndef __APPLE__` guards the evdi
  uinput touch path; the macOS branch stubs `set_input_handler(nullptr)` until
  Phase B (`CGEventPost`) exists.

Keep the Linux `host/` build free of these conditionals — re-introduce them only
if/when the macOS port is actively resumed.
