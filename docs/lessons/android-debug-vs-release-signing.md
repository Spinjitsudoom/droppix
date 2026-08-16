# G-2026-08-16-android-debug-vs-release-signing: `adb install` fails with an empty error because the phone runs a release-signed build

- **ID:** `G-2026-08-16-android-debug-vs-release-signing`
- **Tags:** `android`, `packaging`, `gotcha`, `silent-failure`, `medium`
- **Date:** 2026-08-16

## Symptom

Installing a freshly built debug APK onto a phone that already has droppix fails, and
`adb install` prints **nothing after the colon**:

```
Performing Streamed Install
adb: failed to install .../app-debug.apk:
```

No reason, no error code. Retrying does not help. It is easy to misread this as the
Play Protect scan timeout, which produces the *same* empty message and *does* clear on a
retry.

## Root cause

Two different signing keys, and the empty message hides which problem you have.

Phones are provisioned from `packaging/android/build-apk.sh`, which builds
**`assembleRelease`** signed with the persistent keystore at
`~/droppix-android-build/droppix-release.jks` (delivered to `complete builds/` as
`droppix-<date>-<sha>.apk`). Building `assembleDebug` by hand signs with Gradle's
auto-generated debug keystore instead. Android refuses to update a package whose
signature changed, so the install can never succeed — no amount of retrying fixes it.

`adb install` swallows the reason. Going through `pm` directly shows it:

```
$ adb push app-debug.apk /data/local/tmp/x.apk && adb shell pm install -r -t /data/local/tmp/x.apk
Failure [INSTALL_FAILED_UPDATE_INCOMPATIBLE: Package com.droppix.app signatures do not
match previously installed version; ignoring!]
```

## Fix

Build the APK the same way the phone got its current one — do **not** reach for
`assembleDebug` when updating a real device:

```bash
distrobox enter droppix-android -- bash -lc \
  'export ANDROID_HOME=$HOME/android-sdk; bash "<repo>/packaging/android/build-apk.sh"'
adb install -r "<repo>/complete builds/droppix-<date>-<sha>.apk"
```

This installs as an update, so the saved host address, TLS pin and pairing survive.

**Do not "fix" it by uninstalling first.** Uninstalling drops the trusted-certificate
pin and paired state, forcing a re-pair — and it is unrecoverable if the reinstall then
fails for an unrelated reason (MIUI has refused installs with
`INSTALL_FAILED_USER_RESTRICTED`). Match the signature instead.

## How to detect this in the future

An **empty** `adb install` failure means "ask `pm` for the real reason", never "retry
forever". The two causes it hides:

| Real error | Behaviour |
|---|---|
| `INSTALL_FAILED_UPDATE_INCOMPATIBLE` | Never clears. You built the wrong variant. |
| Play Protect scan timeout | Clears on retry. |

Confirm which build is on the device before rebuilding:

```bash
adb shell dumpsys package com.droppix.app | grep -E 'lastUpdateTime|versionName'
```

Related: the Android build directory is redirected off the CIFS mount
(`android/build.gradle.kts` → `~/droppix-android-build`), so `app/build/outputs/` inside
the repo stays empty and a successful `-q` build looks like it did nothing.
