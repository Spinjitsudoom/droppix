# Press-and-hold Right-click Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Holding one finger still for 600 ms on any droppix client right-clicks exactly under the finger, with no left-click on lift.

**Architecture:**
- A pure `HoldGesture` detector next to `TwoFingerTap` is fed contact updates plus a clock
  tick from the stream loop.
- When it fires, `InputInjector` turns the held contact into an `MT_TOOL_PALM` (libinput then
  sends a touch cancel), drops that finger, and clicks right on the existing
  `droppix-rightclick` pointer.
- The desktop bounds gain a real origin, so the click lands correctly on layouts that don't
  start at 0,0.

**Tech Stack:** C++17, Linux uinput / evdev protocol B, GoogleTest; build in the `droppix-dev` distrobox, build dir `~/droppix-build`.

**Spec:** `docs/superpowers/specs/2026-11-24-press-and-hold-right-click-design.md`

## Global Constraints

- `kHoldMs = 600`; `kHoldSlop = 1000` (0..65535 output space, ≈1.5%).
- Host-only. No wire/protocol change, no client change.
- Normal taps, drags, scrolls and pinches must reach the PC exactly as today, with no added
  latency.
- The two-finger tap keeps working. A sequence in which the hold fired never also produces a
  two-finger right-click.
- Build/test: `distrobox enter droppix-dev -- bash -lc 'cmake --build "$HOME/droppix-build" -j && ctest --test-dir "$HOME/droppix-build"'`
  - Baseline: 368 passed, with the 3 openssl tests skipped.
- Commit straight to `master`, ending with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **The client drops mid-hold.** The session ends and the injector is destroyed, so `tick()`
   must never fire for a dead session. Owner: Task 4 (it only ticks inside the session loop).
2. **The finger keeps moving after the hold fired.** Expect no second right-click and no
   touch movement reaching the PC. Pinned in Task 1:
   `HoldGesture.FiresOnlyOncePerSequence` drags after firing.
3. **A second finger lands after the hold fired.** It must pass through as a normal touch,
   and no two-finger right-click may follow. Pinned in Task 4 by suppressing the tap result
   while `hold_.spent()`.
4. **A slow-but-moving finger whose updates stall for over 600 ms on a Wi-Fi hiccup**
   false-fires. Accepted (spec risk), not tested.
5. **A layout with an output at negative coordinates.** The click must land on the droppix
   output. Pinned in Task 3: `MonitorGeometry.DesktopBoundsHasRealOrigin`.

---

### Task 1: `HoldGesture` detector

**Files:**
- Modify: `host/src/tap_gesture.h`, `host/src/tap_gesture.cpp`
- Test: `host/tests/test_tap_gesture.cpp`

**Interfaces:**
- Produces:

```cpp
class HoldGesture {
 public:
  struct Result { bool fire = false; uint8_t id = 0; uint16_t x = 0; uint16_t y = 0; };
  Result update(const std::vector<TouchContact>& contacts, int64_t now_ms);
  Result tick(int64_t now_ms);
  bool spent() const;   // fired during the current touch sequence
  static constexpr uint16_t kHoldSlop = 1000;
  static constexpr int64_t kHoldMs = 600;
};
```

Behaviour:
- A sequence starts when contacts go from empty to non-empty, and ends when they go empty.
- It fires at most once per sequence, from either `update` or `tick`, when:
  - the sequence never had more than one contact;
  - that contact never moved more than `kHoldSlop` on either axis from its first position;
  - `now_ms - start >= kHoldMs`.
- `x,y` is the contact's latest position; `id` is its id.

- [ ] **Step 1: Write the failing tests** in `test_tap_gesture.cpp`:

```cpp
TEST(HoldGesture, StillFingerFiresAfterHoldTimeAtFingerPosition) {
  HoldGesture g;
  EXPECT_FALSE(g.update({{3, 10000, 20000, 0}}, 0).fire);
  EXPECT_FALSE(g.tick(599).fire);
  auto r = g.tick(600);
  EXPECT_TRUE(r.fire); EXPECT_EQ(r.id, 3); EXPECT_EQ(r.x, 10000); EXPECT_EQ(r.y, 20000);
  EXPECT_TRUE(g.spent());
}
TEST(HoldGesture, SmallJitterStillFires) {
  HoldGesture g;
  g.update({{0, 10000, 20000, 0}}, 0);
  g.update({{0, 10900, 19100, 0}}, 300);                  // within kHoldSlop
  auto r = g.tick(650);
  EXPECT_TRUE(r.fire); EXPECT_EQ(r.x, 10900); EXPECT_EQ(r.y, 19100);
}
TEST(HoldGesture, MovementBeyondSlopNeverFires) {
  HoldGesture g;
  g.update({{0, 10000, 20000, 0}}, 0);
  g.update({{0, 11001, 20000, 0}}, 100);
  g.update({{0, 10000, 20000, 0}}, 200);                  // coming back doesn't re-arm
  EXPECT_FALSE(g.tick(1000).fire);
}
TEST(HoldGesture, SecondFingerCancelsHold) {
  HoldGesture g;
  g.update({{0, 10000, 20000, 0}}, 0);
  g.update({{0, 10000, 20000, 0}, {1, 30000, 20000, 0}}, 100);
  g.update({{0, 10000, 20000, 0}}, 200);
  EXPECT_FALSE(g.tick(1000).fire);
}
TEST(HoldGesture, FiresOnlyOncePerSequence) {
  HoldGesture g;
  g.update({{0, 10000, 20000, 0}}, 0);
  EXPECT_TRUE(g.tick(600).fire);
  EXPECT_FALSE(g.tick(2000).fire);
  EXPECT_FALSE(g.update({{0, 40000, 20000, 0}}, 2100).fire);
}
TEST(HoldGesture, LiftResetsForTheNextSequence) {
  HoldGesture g;
  g.update({{0, 10000, 20000, 0}}, 0);
  g.tick(600);
  g.update({}, 700);
  EXPECT_FALSE(g.spent());
  g.update({{1, 5000, 5000, 0}}, 1000);
  EXPECT_TRUE(g.tick(1600).fire);
}
TEST(HoldGesture, QuickTapNeverFires) {
  HoldGesture g;
  g.update({{0, 10000, 20000, 0}}, 0);
  g.update({}, 120);
  EXPECT_FALSE(g.tick(1000).fire);
}
```

- [ ] **Step 2: Run to verify they fail**
  - Run: the build command.
  - Expected: compile error, `‘HoldGesture’ was not declared`.

- [ ] **Step 3: Implement `HoldGesture`** in `tap_gesture.{h,cpp}`.
  - State: in-sequence flag, start time, tracked id, first position, latest position, a
    `disqualified` flag (second finger or moved beyond slop), a `spent` flag.
  - `update` refreshes the state, then runs the same firing check as `tick`.

- [ ] **Step 4: Run tests**
  - Expected: all `HoldGesture.*` pass, plus the 368 baseline.

- [ ] **Step 5: Commit**: `feat(host): HoldGesture detector for press-and-hold`

### Task 2: `MtSlots::slot_of`

**Files:**
- Modify: `host/src/mt_slots.h`, `host/src/mt_slots.cpp`
- Test: `host/tests/test_mt_slots.cpp`

**Interfaces:**
- Produces: `int MtSlots::slot_of(uint8_t id) const;`. Returns the kernel slot currently
  assigned to `id`, or -1 if none.

- [ ] **Step 1: Failing test**

```cpp
TEST(MtSlots, SlotOfReportsAssignedSlotOrMinusOne) {
  MtSlots s;
  s.update({{7, 0, 0, 0}, {9, 0, 0, 0}});
  EXPECT_GE(s.slot_of(9), 0);
  EXPECT_NE(s.slot_of(9), s.slot_of(7));
  EXPECT_EQ(s.slot_of(42), -1);
  s.update({{7, 0, 0, 0}});
  EXPECT_EQ(s.slot_of(9), -1);
}
```

- [ ] **Step 2: Run; expect a compile failure**
- [ ] **Step 3: Implement** with a lookup in `idSlot_`.
- [ ] **Step 4: Run; expect a pass**
- [ ] **Step 5: Commit**: `feat(host): MtSlots::slot_of`

### Task 3: Desktop bounds with a real origin

**Files:**
- Modify: `host/src/monitor_geometry.cpp` (`desktop_bounds`), `host/src/monitor_geometry.h` (comment)
- Modify: `host/src/input_injector.{h,cpp}` (`set_geometry`, `scale_x`, `scale_y`)
- Modify: `host/src/stream_daemon.cpp:266-276` (the caller)
- Test: `host/tests/test_monitor_geometry.cpp`

**Interfaces:**
- Produces:
  - `Rect desktop_bounds(...)` now returns `{minX, minY, maxRight - minX, maxBottom - minY}`
    over the enabled outputs. It is unchanged for layouts that start at 0,0, so the existing
    tests still pass.
  - `void InputInjector::set_geometry(int out_x, int out_y, int out_w, int out_h, int desk_x, int desk_y, int desk_w, int desk_h);`
  - `scale_x` returns `clamp(outX_ - deskX_ + x_norm * outW_ / 65535, 0, deskW_ - 1)`; `scale_y`
    is the same with the y fields.
  - The `--desktop W H` path passes `desk_x = desk_y = 0`.

- [ ] **Step 1: Failing test**

```cpp
TEST(MonitorGeometry, DesktopBoundsHasRealOrigin) {
  const char* layout =
    "Output: 1 HDMI-A-1 a\n\tenabled\n\tGeometry: -1920,0 1920x1080\n"
    "Output: 2 DVI-I-1 b\n\tenabled\n\tGeometry: 0,0 2560x1600\n";
  Rect b = desktop_bounds(parse_kscreen_outputs(layout));
  EXPECT_EQ(b.x, -1920); EXPECT_EQ(b.y, 0);
  EXPECT_EQ(b.w, 1920 + 2560); EXPECT_EQ(b.h, 1600);
}
```

- [ ] **Step 2: Run; expect a failure** (`b.x == 0`)
- [ ] **Step 3: Implement** `desktop_bounds`, `set_geometry`, `scale_x`/`scale_y`, and the
  `stream_daemon` caller.
- [ ] **Step 4: Run the full suite; expect 368 + new tests to pass**
- [ ] **Step 5: Commit**: `fix(host): map pointer clicks relative to the real desktop origin`

### Task 4: Wire hold → cancel touch → right-click

**Files:**
- Modify: `host/src/input_injector.h`, `host/src/input_injector.cpp`
- Modify: `host/src/stream_daemon.cpp` (stream loop, ~line 336)

**Interfaces:**
- Consumes: `HoldGesture` (Task 1), `MtSlots::slot_of` (Task 2), and `right_click` /
  `scale_*` (Task 3).
- Produces: `void InputInjector::tick();`, which uses the injector's existing `now_ms()`.

Changes:
- `open()`:
  - `UI_SET_ABSBIT ABS_MT_TOOL_TYPE`, with abs range `MT_TOOL_FINGER..MT_TOOL_PALM`.
  - In `inject()`, emit `ABS_MT_TOOL_TYPE = MT_TOOL_FINGER` for each `isNew` assign.
- New members: `HoldGesture hold_;` and `std::set<uint8_t> cancelled_;`.
- `inject(contacts)`:
  - Feed the **unfiltered** contacts to `hold_.update` and handle a fire.
  - Then filter out ids in `cancelled_` before `slots_.update`, the BTN_TOUCH/primary-finger
    emulation, and `tap_.update`. A cancelled finger is released by MtSlots as "lifted" on
    the next update.
  - Drop the `tap_` result while `hold_.spent()`.
  - When the unfiltered contacts are empty, clear `cancelled_`.
- `tick()`: `hold_.tick(now_ms())` and handle a fire.
- Fire handling (`private: void on_hold(const HoldGesture::Result&)`):
  - `slot = slots_.slot_of(r.id)`. If `slot >= 0`, emit `ABS_MT_SLOT slot`,
    `ABS_MT_TOOL_TYPE MT_TOOL_PALM`, `SYN_REPORT`.
  - Insert `r.id` into `cancelled_`.
  - Call `right_click(r.x, r.y)`.
- `stream_daemon`: call `injector.tick();` once per stream-loop iteration, beside the existing
  `tx_.poll_control()` calls. Doing it once at the top of the `while` body is enough; with
  touch on, `frame_timeout` is 8 ms.

- [ ] **Step 1: Build plus the full suite.** Expect 368 + Task 1-3 tests to pass. The injector
  writes to uinput and has no unit test; the logic lives in the Task 1 detector.
- [ ] **Step 2: Deploy and check on the device**
  - Build: `bash packaging/appimage/build-appimage.sh`.
  - Relaunch the newest AppImage from `~/Downloads`.
  - Reconnect the tablet:
    `adb shell am force-stop com.droppix.app && adb shell monkey -p com.droppix.app -c android.intent.category.LAUNCHER 1`.
  - The user holds a finger on the desktop and in Dolphin. Expected:
    - the context menu opens under the finger;
    - the menu stays open after lifting (no left-click);
    - a quick tap still left-clicks;
    - dragging still works.
- [ ] **Step 3: Docs**
  - Spec Status → `Shipped on master (2026-11-24).`
  - `docs/STATUS.md` touch row: add "press-and-hold (600 ms) right-click".
  - README "What it does" touch bullet: add press-and-hold.
- [ ] **Step 4: Commit**:
  `feat(host): press-and-hold right-click at the finger, without a left-click on lift`
