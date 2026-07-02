#!/usr/bin/env bash
# Build a SIGNED release APK that installs on any Android phone by tapping it (enable
# "install unknown apps" for your file manager — no developer options / adb needed).
#
# The signing keystore + its password are generated once and kept OFF the repo (in
# $DROPPIX_ANDROID_KS_DIR, default ~/droppix-android-build). Reusing the same keystore
# keeps the app signature stable so future builds install as updates. Delivered into
# "complete builds/".
#
# Run inside the droppix-android distrobox (has the Android SDK + JDK):
#   distrobox enter droppix-android -- bash -lc 'bash "<repo>/packaging/android/build-apk.sh"'
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KSDIR="${DROPPIX_ANDROID_KS_DIR:-$HOME/droppix-android-build}"
KS="$KSDIR/droppix-release.jks"
PWFILE="$KSDIR/droppix-release.pass"
mkdir -p "$KSDIR"

# Generate the release keystore + a random password once.
if [ ! -f "$KS" ]; then
  PW="$(head -c 24 /dev/urandom | base64 | tr -dc 'A-Za-z0-9' | head -c 24)"
  printf '%s' "$PW" > "$PWFILE"; chmod 600 "$PWFILE"
  keytool -genkeypair -keystore "$KS" -alias droppix -keyalg RSA -keysize 2048 \
    -validity 10000 -storepass "$PW" -keypass "$PW" -dname "CN=droppix" >/dev/null
  echo "generated release keystore at $KS"
fi
PW="$(cat "$PWFILE")"
export DROPPIX_KEYSTORE="$KS" DROPPIX_KS_PASS="$PW" DROPPIX_KEY_ALIAS=droppix DROPPIX_KEY_PASS="$PW"

cd "$REPO/android"
bash gradlew --no-daemon assembleRelease

# The Android buildDir is redirected off the CIFS mount to ~/droppix-android-build.
APK="$( { find "$HOME/droppix-android-build" "$REPO/android/app/build" -name 'app-release.apk' 2>/dev/null || true; } | head -1)"
[ -n "$APK" ] || { echo "error: app-release.apk not found after build" >&2; exit 1; }

DEST="$REPO/complete builds"; mkdir -p "$DEST"
SHA="$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo nogit)"
OUT="$DEST/droppix-$(date +%Y%m%d-%H%M)-$SHA.apk"
cp "$APK" "$OUT"
echo "delivered signed APK: $OUT"
