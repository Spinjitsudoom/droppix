# L-2026-08-17-audio-latency-grows-unbounded: every audio buffer bounded memory, none bounded lag

- **ID:** `L-2026-08-17-audio-latency-grows-unbounded`
- **Tags:** `host`, `android`, `client`, `audio`, `performance`, `wrong-answer`, `high`
- **Date:** 2026-08-17
- **Related:** `slow-link-bufferbloat` (same shape, video path), `static-screen-looks-like-low-fps`

## Symptom

Audio drifted seconds behind the host on **both** the Android and the web client, and stayed
there. Not a constant offset (which would be plain latency) — it grew and never recovered.

## Root cause

Four buffers on the path. Every one bounded *memory*; not one bounded *time*.

| Where | Bound | Why it did not help |
|---|---|---|
| `parec` capture | none (`--latency-msec` unset) | PulseAudio picks a server default sized for reliability — hundreds of ms before a byte reaches us |
| `AudioStreamer::queue_` | none | drained inside the video loop, so any stall (slow link, static screen, long encode) piles up PCM at 192 kB/s |
| Android `AudioPlayer.queue` | 64 chunks ≈ **1.4 s** | dropped the oldest *one* chunk per overflow, so it sat pinned at the ceiling forever |
| Web `nextTime` cursor | lower bound only | underrun was handled; nothing capped how far **ahead** it could book |

The decisive property: **playback only advances in real time, so a backlog is never caught
up.** Once audio is late it stays late until something throws audio away. Both clients were
built to never throw anything away — the Android queue drop-oldest kept depth at maximum
rather than reducing it, and the web scheduler had no upper bound at all.

A single stall was therefore permanent. That is why it looked like drift.

## Fix

Bound the *duration* at every stage, and on overflow drop to a **low-water mark** rather than
to the limit:

```cpp
// host/src/audio_latency.h — 200 ms budget, recover to half of it
inline size_t audio_overflow_drop_bytes(size_t queued) {
  const size_t limit = audio_max_queue_bytes();
  return queued <= limit ? 0 : queued - limit / 2;
}
```

- `parec --latency-msec=20` caps the capture buffer at the source.
- `AudioStreamer` tracks queued bytes and discards the oldest past 200 ms.
- Android: 12 chunks (~256 ms) max, dropping to 4 (~85 ms) on overflow.
- Web: `scheduleAudio()` caps the lead at 250 ms and resyncs to +60 ms when blown.

Dropping to the limit is the trap — it leaves the buffer exactly full, so the very next chunk
overflows again and the stream stays maximally late. Dropping to a fraction buys headroom, so
one glitch costs one audible gap instead of permanent lag. This is asserted directly
(`AudioLatency.OverflowDropsToLowWaterNotToTheLimit`).

## How to detect this in the future

**Lag that grows and never recovers is an unbounded queue; lag that is constant is genuine
latency.** They need opposite fixes, so establish which one you have before touching
anything.

For any buffer on a real-time path, ask what it is bounded in. A cap in *items* or *bytes* is
a memory bound; latency needs a bound in *milliseconds*, plus a recovery rule that reduces
depth rather than merely capping it. A queue that is always full is the same as a queue that
is too big.

The video path had already learned this twice (`paint-policy.ts` caps video lead; capture is
paced on `SIOCOUTQ` backlog). Audio simply had not.
