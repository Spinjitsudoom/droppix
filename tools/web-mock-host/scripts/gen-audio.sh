#!/usr/bin/env bash
# Build a looping s16le 48kHz stereo PCM bed: short speech + music chords.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="$ROOT/assets"
mkdir -p "$OUT_DIR"
MUSIC="$OUT_DIR/music.pcm"
SPEECH="$OUT_DIR/speech.pcm"
MIX="$OUT_DIR/loop.pcm"
TMP="$OUT_DIR/_tmp"
mkdir -p "$TMP"

# ~8s chord bed (C major arpeggio-ish)
ffmpeg -y -hide_banner -loglevel error \
  -f lavfi -i "sine=frequency=261.63:sample_rate=48000:duration=8" \
  -f lavfi -i "sine=frequency=329.63:sample_rate=48000:duration=8" \
  -f lavfi -i "sine=frequency=392.00:sample_rate=48000:duration=8" \
  -f lavfi -i "sine=frequency=523.25:sample_rate=48000:duration=8" \
  -filter_complex "\
    [0]volume=0.18[a0];\
    [1]volume=0.14[a1];\
    [2]volume=0.12[a2];\
    [3]volume=0.08[a3];\
    [a0][a1][a2][a3]amix=inputs=4:duration=longest,\
    atrim=0:8,afade=t=in:st=0:d=0.4,afade=t=out:st=7.4:d=0.6" \
  -ar 48000 -ac 2 -f s16le "$MUSIC"

# Spoken line (macOS `say`, else ffmpeg sine fallback labeled as speech-ish)
if command -v say >/dev/null 2>&1; then
  say -v Samantha -o "$TMP/speech.aiff" \
    "Droppix mock host. You are hearing the server audio path. Click the video to send input."
  ffmpeg -y -hide_banner -loglevel error -i "$TMP/speech.aiff" \
    -ar 48000 -ac 2 -f s16le "$SPEECH"
else
  # Fallback: short descending tones standing in for speech
  ffmpeg -y -hide_banner -loglevel error \
    -f lavfi -i "sine=frequency=600:sample_rate=48000:duration=0.2" \
    -f lavfi -i "sine=frequency=500:sample_rate=48000:duration=0.2" \
    -f lavfi -i "sine=frequency=400:sample_rate=48000:duration=0.35" \
    -filter_complex "[0][1][2]concat=n=3:v=0:a=1,volume=0.35" \
    -ar 48000 -ac 2 -f s16le "$SPEECH"
fi

# Pad speech to 8s with silence, then mix under music
SPEECH_LEN=$(wc -c < "$SPEECH" | tr -d ' ')
NEED=$((48000 * 2 * 2 * 8)) # 8s stereo s16le
python3 - <<PY
from pathlib import Path
need = $NEED
speech = Path("$SPEECH").read_bytes()
if len(speech) > need:
    speech = speech[:need]
else:
    speech = speech + b"\x00" * (need - len(speech))
music = Path("$MUSIC").read_bytes()
# mix: 0.55 music + 0.9 speech (clip)
import array
m = array.array("h")
s = array.array("h")
m.frombytes(music[:need])
s.frombytes(speech)
out = array.array("h")
for i in range(len(m)):
    v = int(m[i] * 0.45 + s[i] * 0.85)
    if v > 32767: v = 32767
    if v < -32768: v = -32768
    out.append(v)
Path("$MIX").write_bytes(out.tobytes())
print(f"wrote $MIX ({need} bytes, {need/48000/4:.1f}s)")
PY

rm -rf "$TMP"
echo "audio ready: $MIX"
