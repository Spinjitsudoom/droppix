# G-2026-07-19-zxing-embedded-minsdk: zxing-android-embedded 4.x needs minSdk 24

- **ID:** `G-2026-07-19-zxing-embedded-minsdk`
- **Tags:** `android`, `packaging`, `gotcha`, `regression`, `high`, `gotcha`
- **Date:** 2026-07-19
- **Related:** QR pairing (`docs/superpowers/specs/2026-07-19-qr-code-pairing-design.md`)

## Symptom

QR-scan pairing built against `com.journeyapps:zxing-android-embedded:4.3.0` compiles fine (compileSdk 34) but would crash at runtime the moment the user taps "Scan QR to Pair" on the Nexus 10 (Android 5.1.1 / **API 22**) — droppix's primary target device. Manifest-merge/desugar issues, not caught by a build on a modern emulator.

## Root cause

The androidx-based `zxing-android-embedded` 4.x line declares **minSdk 24**. droppix is `minSdk = 21`. There is **no androidx release of the library that supports below API 24** — 4.0.0 moved to androidx and bumped minSdk to 24 in the same release. Pinning 4.3.0 in a minSdk-21 app is a latent runtime crash on API 21–23.

## Fix

Use `zxing-android-embedded:3.6.0` (last line supporting API 19+). It pulls the **legacy** android support library, so:

```kotlin
// app/build.gradle.kts
implementation("com.journeyapps:zxing-android-embedded:3.6.0")  // NOT 4.x; core left transitive (3.3.x)
```
```properties
# gradle.properties
android.enableJetifier=true   # rewrites 3.6.0's support-lib to androidx
```

The scan API also differs: 3.6.0 uses `IntentIntegrator(...).initiateScan()` + `onActivityResult` → `IntentIntegrator.parseActivityResult(...)` (the androidx `ScanContract`/`registerForActivityResult` is 4.x-only). See `ConnectActivity.kt`.

## How to detect this in the future

- Before adding any Android dependency to droppix, check the library's declared `minSdkVersion` against `app/build.gradle.kts` `minSdk = 21`. A library minSdk > 21 is a runtime crash on the Nexus 10, invisible on a modern emulator.
- If a dep needs the legacy support library in an `android.useAndroidX=true` app, set `android.enableJetifier=true`.
- Whenever a QR/scan feature is touched, verify on an **API 22** emulator/device, not just a current one.
