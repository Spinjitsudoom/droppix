# Multi-touch — design

**Date:** 2026-07-02
**Status:** approved

## Goal

Forward all fingers from the tablet so the droppix monitor is a real multi-touch
touchscreen — pinch-zoom, two-finger scroll, rotate, etc. work as the KDE desktop/apps
interpret them. Single-finger tap/drag is the 1-contact case, unchanged. (Multi-*device*
is a separate future project.)

## Model: full contact set per event

On every touch event the tablet sends the COMPLETE set of currently-active contacts
(id, x, y, pressure). An empty set = all fingers lifted. The host diffs consecutive sets
to drive the kernel touchscreen. Robust: a dropped event can't leave a finger stuck down.

## Components

### 1. Protocol — new `Touch` message (host `protocol.{h,cpp}` + Android `Protocol.kt`)

- `enum MsgType { ... Touch = 11 }`. The single-pointer `Input = 7` stays for back-compat.
- `struct TouchContact { uint8_t id; uint16_t x; uint16_t y; uint16_t pressure; };`
- Body: `u8 count`, then `count ×` `{ u8 id, u16 x, u16 y, u16 pressure }` (all big-endian).
  Capped at 10 contacts. `count == 0` = all up.
- `encode_touch(contacts)` / `decode_touch(body, out)`; Kotlin `encodeTouch(list)` byte-identical.
- Tests: round-trip, wire-layout (exact bytes), empty set; shared C++/Kotlin vector.

### 2. Pure slot mapper — `host/src/mt_slots.{h,cpp}` (unit-tested, no I/O)

```cpp
class MtSlots {
 public:
  explicit MtSlots(int maxSlots = 10);
  struct Assign { int slot; TouchContact c; bool isNew; };  // isNew => set TRACKING_ID
  struct Update { std::vector<Assign> active; std::vector<int> lifted; };  // lifted = slots to release
  Update update(const std::vector<TouchContact>& contacts);   // mutates the id->slot map
 private:
  int maxSlots_;
  std::map<uint8_t,int> idSlot_;
};
```

`update()`: contacts whose id disappeared → their slots go to `lifted` (and are freed);
each remaining/new contact gets its existing slot or the lowest free slot (`isNew=true`);
contacts beyond `maxSlots` free slots are dropped. Tested: assign/hold/lift/reuse-slot/overflow.

### 3. Host injector — `InputInjector` becomes multi-touch protocol B

- `open()` adds `ABS_MT_SLOT` (0..9), `ABS_MT_TRACKING_ID`, `ABS_MT_POSITION_X`/`Y`
  (0..65535), `ABS_MT_PRESSURE` (0..1023), keeping `ABS_X`/`Y`/`ABS_PRESSURE` (+ `BTN_TOUCH`,
  `INPUT_PROP_DIRECT`) for single-touch emulation of the primary finger.
- `inject(const std::vector<TouchContact>& contacts)`: run `MtSlots::update`; for each
  `lifted` slot emit `MT_SLOT` + `TRACKING_ID = -1`; for each `active` emit `MT_SLOT`,
  `TRACKING_ID` (if new), `MT_POSITION_X/Y`, `MT_PRESSURE`; emit `BTN_TOUCH` on 0↔n
  transition; mirror the primary contact to `ABS_X/Y/PRESSURE`; one `SYN_REPORT`.

### 4. Dispatch — `transport_server` + `stream_daemon`

- Replace `set_input_handler`/`input_handler_` with `set_touch_handler(std::function<void(
  const std::vector<TouchContact>&)>)`. `poll_control`: `Touch` → `decode_touch` → handler;
  legacy `Input` → `decode_input` → handler with a 1-contact set (`action==2` up → empty),
  so old clients still work.
- `stream_daemon`: `tx_.set_touch_handler([&injector](const auto& c){ injector.inject(c); });`

### 5. Android

- `DisplaySurfaceView`: `interface TouchListener { fun onTouch(contacts: List<Contact>) }`
  with `data class Contact(val id: Int, val x: Int, val y: Int, val pressure: Int)`.
  `onTouchEvent` iterates all pointers (`pointerCount`, `getPointerId/getX/getY/getPressure(i)`),
  normalizing x/y→0..65535, pressure→0..1023; on `ACTION_POINTER_UP`/`ACTION_UP` it excludes
  the lifting pointer (`actionIndex`); `ACTION_CANCEL` sends an empty set; keeps the MOVE
  throttle. Caps at 10 contacts.
- `TransportClient.sendTouch(contacts)` (background sender, like sendInput).
- `StreamActivity` onTouch override → `client.sendTouch(contacts)`.
- `ProtocolTest`: `encodeTouch` byte-match vector.

## Testing

- Host: `decode/encode_touch` (round-trip, wire-layout, empty) + `MtSlots` (assign/lift/reuse/
  overflow) unit tests. Android: `encodeTouch` byte-match.
- Manual on-device: two-finger pinch/scroll on the droppix monitor moves/zooms as expected;
  single-finger unchanged.

## Out of scope

- Multi-device / multiple monitors (separate project).
- Gesture interpretation — the desktop/apps do that from the real MT device.
