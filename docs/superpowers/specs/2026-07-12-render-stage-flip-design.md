# Android GL render stage + horizontal flip

**Date:** 2026-07-12
**Status:** Shipped on master.
**Roadmap:** tier T1 "Flipped display" — sub-project A of the T1 remainder (sub-project B = Mouse input, separate).

## Summary

Add a **horizontal flip** of the streamed image to both clients (Spacedesk "Flipped Display", for rear-projection / mirror installs). Flip is a **client-only display transform — no host or protocol change**.

- **Android:** the flip requires CPU/GPU access to the frame, which today's zero-copy `MediaCodec → SurfaceView` path doesn't give. So we build a **GL render stage**: `MediaCodec → SurfaceTexture (external OES) → GLSurfaceView` drawing a fullscreen quad through a fragment shader. Flip is a texture-coordinate transform. The shader is structured so **brightness/contrast (roadmap T2) later drop in as uniforms** with no re-architecture — this render stage is the shared foundation for all client-side image adjustments.
- **Linux client:** cheap — `QVideoFrameFormat::setMirrored(true)` on the pushed frame; `QVideoWidget` renders it mirrored.

## Decisions

| Question | Decision |
| --- | --- |
| Android render approach | GL shader pipeline (SurfaceTexture + GLSurfaceView), not TextureView-matrix — it also unlocks T2 brightness/contrast. |
| Accepted cost | The GL stage replaces the current zero-copy `SurfaceView` path (one GPU texture copy, ~≤1 frame added latency, more code). Chosen deliberately for flip + future color. |
| Touch when flipped | Flip **video only**, NOT the touch mapping. Matches Spacedesk's view-only flipped display; touching a flipped display hits the mirror-image host location — documented limitation, not remapped. |
| Protocol/host | No change. Client-only. |
| Scope | Horizontal flip only (the mirror-install case). No vertical flip. |

## Android GL render pipeline

**New file `ui/GlDisplayView.kt`** — `class GlDisplayView : GLSurfaceView` that:
- Sets EGL context 2.0, `RENDERMODE_WHEN_DIRTY`, and an inner `Renderer`.
- Carries over the **exact touch→contacts logic** from the current `DisplaySurfaceView` (GLSurfaceView is a `SurfaceView` subclass, so `onTouchEvent` works unchanged). The existing `SurfaceListener`/`TouchListener` interfaces are preserved so `StreamActivity` wiring changes minimally.
- Exposes `@Volatile var flipHorizontal: Boolean` (read by the renderer each draw).

**Inner `Renderer`:**
- `onSurfaceCreated`: compile the shader program; create one **external-OES texture** (`GL_TEXTURE_EXTERNAL_OES`); create a `SurfaceTexture(texId)`; create a `Surface(surfaceTexture)`; set `surfaceTexture.setOnFrameAvailableListener { requestRender() }`; hand the `Surface` to the activity via the existing `SurfaceListener.onSurfaceReady(surface)` (this Surface replaces the old SurfaceHolder surface that `VideoDecoder` decoded into).
- `onSurfaceChanged`: `glViewport`.
- `onDrawFrame`: `surfaceTexture.updateTexImage()`; `surfaceTexture.getTransformMatrix(stMatrix)`; build `uTexMatrix` = `stMatrix`, and when `flipHorizontal` is on, **pre-multiply a horizontal-mirror matrix** (mirror about S=0.5: translate +0.5, scale S by −1, translate −0.5) into it. Upload `uTexMatrix` and draw the fullscreen quad. This one uniform carries both the codec's crop/orientation (`stMatrix`, always applied) and the flip — the quad UVs stay fixed, so there is a single, unambiguous flip mechanism.

**Shaders** (`ui/GlDisplayView.kt` string constants):
- Vertex: passes through position + texcoord (texcoord transformed by a `uTexMatrix` uniform = `stMatrix` combined with the flip).
- Fragment: `#extension GL_OES_EGL_image_external : require`, `samplerExternalOES uTex`, `gl_FragColor = texture2D(uTex, vTexCoord);`. **T2 hook:** brightness/contrast will be added here as `uniform float uBrightness, uContrast;` applied to the sampled color — left out now, but the uniform-application shape is noted in a comment so T2 is a drop-in.

**`VideoDecoder`** is unchanged internally — `configure(fmt, surface, null, 0)` still decodes to whatever `Surface` it's given; it now gets the SurfaceTexture-backed one.

**`activity_stream.xml` + `StreamActivity`:** replace `DisplaySurfaceView @+id/surface` with `GlDisplayView @+id/surface`; keep the overlay `TextView` and settings button on top (FrameLayout order unchanged). `StreamActivity` sets `surface.flipHorizontal = settings.flipHorizontal` in `startStreaming()` (re-read on the settings-reconnect); everything else (`SurfaceListener`/`TouchListener` wiring, `onSurfaceReady → startStreaming`) is unchanged. The old `DisplaySurfaceView.kt` is removed once `GlDisplayView` subsumes it.

## Linux client flip

- `ClientSettings.flip_horizontal = false`.
- In `client/src/video_decoder.cpp::submit`, after building `QVideoFrameFormat fmt(...)`, call `fmt.setMirrored(flip)` when the setting is on (thread the flag into the decoder, e.g. a setter `VideoDecoder::setFlipHorizontal(bool)` read on the video thread). `QVideoWidget`/`QVideoSink` render mirrored. Applied live on the settings-change reconnect (same path the other client settings use).

## Settings / UI

- **Android:** `AppSettings.flipHorizontal: Boolean = false` (SharedPreferences key `flip_h`); a "Flip horizontal" `Switch @+id/flip_switch` in `activity_settings.xml` + wiring in `SettingsActivity` (seed + save, extending the Save `AppSettings(...)` call).
- **Linux:** `ClientSettings.flip_horizontal` (QSettings key `flip_h`); a `QCheckBox* flip_` in `ClientSettingsDialog`, seeded from + written to settings.

## Testing

- **Android:** the GL renderer can't be meaningfully unit-tested (needs an EGL/GPU context). Gate on: clean `assembleDebug`; an on-device pass — flip OFF renders normally, flip ON mirrors the image horizontally, touch still controls the host, the perf overlay/HUD still draws, and there's no visible added stutter vs before.
- **Linux:** a small unit test that the decoder sets `QVideoFrameFormat::isMirrored()` on produced frames when flip is enabled and not when disabled.
- **Both:** settings persistence round-trip for the new flag (Android `AppSettingsTest`, Linux `test_client_settings`).

## Out of scope

- **Brightness / contrast** (roadmap T2) — this render stage is built to host them (fragment-shader uniforms on Android; a swscale/`QVideoSink` color pass on Linux), but they are a separate follow-up.
- **Android vertical flip / rotation via GL** — not needed (rotation is handled host-side).
- **Mouse input** — sub-project B, separate spec.
- **Remapping touch when flipped** — deliberately not done (view-only flip).
