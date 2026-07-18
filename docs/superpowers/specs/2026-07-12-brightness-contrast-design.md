# Brightness / Contrast (client-side image adjustment)

**Date:** 2026-07-12
**Status:** Shipped on master.
**Roadmap:** tier T2 — the first T2 feature, reusing the Android GL render stage built for flip.

## Summary

Client-side **brightness** and **contrast** adjustment of the streamed image (independent of the device's hardware backlight). Android applies them as GL fragment-shader uniforms (the render stage already left a hook for exactly this); the Linux client applies them as a per-pixel transform on the decoded luma plane. **Client-only — no host or protocol change.** On the **Linux client**, changes apply **live** with no reconnect (the decoder's brightness/contrast fields are updated in place). On **Android**, opening Settings stops streaming and returning restarts it (a brief reconnect/blink), same as every other setting including flip; the freshly-loaded brightness/contrast values are re-applied when the stream restarts, so the feature works but is not seamless.

## Decisions

| Question | Decision |
| --- | --- |
| Controls | Two sliders each: **Brightness** (−100…+100, default 0) and **Contrast** (0…200 %, default 100). |
| Apply timing | **Linux:** live — pushed straight to the decoder in place, no reconnect. **Android:** values are pushed to the renderer immediately when set, but opening/returning from Settings still stops and restarts streaming (like every other setting), so a change made in Settings is only visible after that reconnect. |
| Android impl | Fragment-shader uniforms `uBrightness`/`uContrast` (the T2 hook already in the shader). |
| Linux impl | Per-pixel transform on the Y (luma) plane during the decoder's frame copy; chroma untouched. |
| Value mapping | `uBrightness = brightness/200` (≈ ±0.5 on 0..1 rgb); `uContrast = contrast/100` (0..2, 100→neutral 1.0). Linux luma mirrors this on 0..255. |
| Protocol/host | No change. |

## Settings (`AppSettings` / `ClientSettings`)

- Android `AppSettings`: `brightness: Int = 0` (−100…100), `contrast: Int = 100` (0…200); persisted keys `brightness`, `contrast`.
- Linux `ClientSettings`: `brightness = 0`, `contrast = 100`; keys `brightness`, `contrast`.

## Android (`GlDisplayView.kt`, `SettingsActivity`, `StreamActivity`)

- **Shader:** replace the hook comment in `FRAG` with real uniforms:
  ```glsl
  uniform float uBrightness;   // -0.5..0.5
  uniform float uContrast;     // 0..2
  ...
  vec4 c = texture2D(uTex, vTexCoord);
  c.rgb = (c.rgb - 0.5) * uContrast + 0.5 + uBrightness;
  gl_FragColor = vec4(clamp(c.rgb, 0.0, 1.0), c.a);
  ```
- `GlDisplayView`: `@Volatile var brightness: Int = 0` and `@Volatile var contrast: Int = 100` (UI-set / GL-read). Cache the two uniform locations in `onSurfaceCreated`; in `onDrawFrame` upload `glUniform1f(uBrightness, brightness/200f)` and `glUniform1f(uContrast, contrast/100f)`.
- **Apply timing:** `StreamActivity.startStreaming()` sets `surface.brightness/contrast` from settings; ADDITIONALLY, `onResume` (after returning from Settings) pushes the freshly-loaded `brightness`/`contrast` onto `surface` directly (harmless/defensive, mirrors the flip pattern). In practice, opening `SettingsActivity` drives `onPause()` -> `stopStreaming()`, and returning drives `onResume()` -> `startStreaming()`, which reconnects (new HELLO, brief black blink) — same as every other setting. So a brightness/contrast change is re-applied on the fresh stream after that reconnect; it is NOT a seamless no-reconnect live-apply. (Brightness/contrast are NOT added to any *additional* reconnect-triggering condition — they simply ride along with the reconnect that already happens on every Settings visit.)

## Linux client (`video_decoder.{h,cpp}`, `settings_dialog`, `main_window.cpp`)

- **Pure helper** (unit-tested): `int droppix::adjust_luma(int y, int brightness, int contrast)` = `clamp((y-128)*contrast/100 + 128 + brightness*255/200, 0, 255)`.
- `VideoDecoder`: `setBrightness(int)` / `setContrast(int)` (fields read per-frame). In `submit`'s plane-copy loop, for **plane 0** only, when brightness≠0 or contrast≠100, transform each byte via `adjust_luma(...)` instead of the plain `memcpy` (planes 1/2 keep the `memcpy` fast path). When neutral (0/100), keep the existing `memcpy` for all planes (no per-pixel cost).
- **Live-apply (seamless, no reconnect):** `main_window` pushes `settings_.brightness/contrast` to the decoder whenever settings change (the decoder reads them on the next frame, in place); brightness/contrast are NOT part of the `onSettingsAction` reconnect `changed` condition. This is the one client where the change is visible on the *existing* stream with no blink.

## UI

- **Android** `activity_settings.xml`: a "Brightness" label + `SeekBar @+id/brightness_seek` and a "Contrast" label + `SeekBar @+id/contrast_seek`, each with a small value `TextView`. `SeekBar` has no negative min on this min-SDK, so Brightness uses `max=200` with displayed value `progress-100`; Contrast uses `max=200`, value `progress`. `SettingsActivity` seeds from `cur`, updates the value label on change, and saves `progress` mapped back.
- **Linux** `ClientSettingsDialog`: two `QSlider` (Horizontal) — Brightness range −100…100, Contrast 0…200 — each with a value label; seeded from `s`, read in `result()`.

## Testing

- **Linux:** unit-test `adjust_luma` with concrete assertions: neutral is identity (`adjust_luma(y,0,100)==y` for y=0/60/128/200); brightness raises/lowers luma (`adjust_luma(100,50,100) > 100`, `adjust_luma(150,-50,100) < 150`); contrast>100 pushes away from 128 (`adjust_luma(200,0,200) > 200`, `adjust_luma(60,0,200) < 60`); contrast<100 pulls toward 128 (`128 < adjust_luma(200,0,50) < 200`); and every result is clamped to `[0,255]` (`adjust_luma(250,100,200)==255`, `adjust_luma(10,-100,100)==0`). Plus a `ClientSettings` persistence round-trip for `brightness`/`contrast`.
- **Android:** `AppSettings` persistence round-trip; the shader itself is verified on-device (GL isn't unit-testable).
- **On-device:** sliders visibly brighten/darken and raise/lower contrast, live (no stream blink); neutral = unchanged image; other settings unaffected.

## Known differences between clients

- **Apply timing:** Linux applies brightness/contrast live on the existing stream (no reconnect). Android re-applies the values when the stream restarts on returning from Settings (a brief blink) — same as every other Android setting, including flip. Not seamless, but the feature works.
- **Color math:** Android applies the adjustment in the fragment shader on RGB (post YUV→RGB conversion), so contrast also affects saturation slightly and the pivot is 127.5 (`0.5` in normalized 0..1 space). The Linux client transforms only the Y (luma) plane, so chroma/saturation is unchanged and the pivot is 128. This is an accepted design tradeoff — Y-only keeps chroma untouched and preserves the neutral memcpy fast path on Linux.

## Out of scope

- Saturation / hue / gamma / color temperature (only brightness + contrast).
- Per-monitor or scheduled adjustment.
- No host/protocol change.
