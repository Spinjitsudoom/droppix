#!/usr/bin/env bash
# Ensure assets/sample.mp4 exists: a real movie clip with spoken dialogue.
# Priority: DROPPIX_MOCK_MP4_URL → Tears of Steel segment (dialogue) →
#           Sintel trailer (music) → locally built test clip.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-${DROPPIX_MOCK_MP4:-$ROOT/assets/sample.mp4}}"
mkdir -p "$(dirname "$OUT")"

if [[ -f "$OUT" && -s "$OUT" ]]; then
  echo "sample exists: $OUT"
  exit 0
fi

encode_common=(-c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p
  -profile:v baseline -level 3.1
  -c:a aac -ar 48000 -ac 2 -b:a 160k -movflags +faststart)

if [[ -n "${DROPPIX_MOCK_MP4_URL:-}" ]]; then
  echo "downloading $DROPPIX_MOCK_MP4_URL"
  curl -fL --progress-bar -o "$OUT.tmp.mp4" "$DROPPIX_MOCK_MP4_URL"
  mv "$OUT.tmp.mp4" "$OUT"
  echo "ready: $OUT"
  exit 0
fi

# Tears of Steel (CC-BY, Blender Foundation): opening bridge dialogue ~0:45-2:20.
TOS_URL="https://download.blender.org/demo/movies/ToS/tears_of_steel_720p.mov"
echo "fetching Tears of Steel dialogue segment (CC-BY blender.org)…"
if ffmpeg -y -hide_banner -loglevel error \
  -ss 30 -i "$TOS_URL" -t 110 \
  "${encode_common[@]}" \
  "$OUT.tmp.mp4" 2>/dev/null; then
  mv "$OUT.tmp.mp4" "$OUT"
  echo "ready: $OUT (Tears of Steel segment)"
  exit 0
fi
rm -f "$OUT.tmp.mp4"

# Fallback: Sintel trailer (music/ambience, no dialogue) - small + reliable.
SINTEL_URL="https://media.w3.org/2010/05/sintel/trailer.mp4"
echo "ToS failed; fetching Sintel trailer…"
if curl -fL --max-time 60 -o "$OUT.dl" "$SINTEL_URL"; then
  ffmpeg -y -hide_banner -loglevel error -i "$OUT.dl" "${encode_common[@]}" "$OUT.tmp.mp4"
  mv "$OUT.tmp.mp4" "$OUT"
  rm -f "$OUT.dl"
  echo "ready: $OUT (Sintel trailer)"
  exit 0
fi
rm -f "$OUT.dl" "$OUT.tmp.mp4"

# Last resort: local build (test pattern + spoken counting via macOS say).
echo "network fetch failed; building local clip…"
TMP="$ROOT/assets/_tmp"
mkdir -p "$TMP"
if command -v say >/dev/null 2>&1; then
  say -v Samantha -o "$TMP/speech.aiff" \
    "Droppix mock stream. One. Two. Three. Four. Five. Six. Seven. Eight. Nine. Ten."
  ffmpeg -y -hide_banner -loglevel error -i "$TMP/speech.aiff" "$TMP/speech.wav"
else
  ffmpeg -y -hide_banner -loglevel error -f lavfi -i "sine=frequency=440:duration=6" "$TMP/speech.wav"
fi
ffmpeg -y -hide_banner -loglevel error \
  -f lavfi -i "testsrc2=size=1280x720:rate=30" -i "$TMP/speech.wav" -t 10 \
  "${encode_common[@]}" -shortest "$OUT"
rm -rf "$TMP"
echo "ready: $OUT (local build)"
