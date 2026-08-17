#pragma once
#include <cstddef>

namespace droppix {

// Raw capture format: s16le, 48 kHz, stereo => 4 bytes per frame.
inline constexpr int kAudioRate = 48000;
inline constexpr int kAudioBytesPerFrame = 4;
inline constexpr size_t kAudioBytesPerSec = kAudioRate * kAudioBytesPerFrame;   // 192000

// How much captured audio the host may hold before it starts discarding.
//
// Audio is drained inside the video loop, so anything that stalls that loop (a slow link, a
// long encode, a static screen waiting on damage) lets PCM pile up. Uncompressed audio is
// 192 kB/s, so a stall of a couple of seconds queues hundreds of kilobytes that will be sent
// late and played late — the stream stays *behind* rather than catching up, because playback
// only ever advances in real time.
//
// The queue is therefore a latency budget, not a safety net. Keep it short.
inline constexpr int kAudioMaxQueueMs = 200;

inline size_t audio_max_queue_bytes() {
  return kAudioBytesPerSec * kAudioMaxQueueMs / 1000;
}

// Capture-side buffering requested from parec (--latency-msec). PulseAudio otherwise picks a
// server default sized for reliability rather than latency, which can be hundreds of
// milliseconds before a single byte reaches us.
inline constexpr int kAudioCaptureLatencyMs = 20;

// Bytes to drop so a backlog returns to the budget.
//
// Deliberately drops down to a LOW-WATER mark rather than trimming to exactly the limit:
// shaving off just the excess leaves the queue pinned at the ceiling, so every later chunk
// arrives late for as long as the stream lasts. Cutting back to a fraction of the budget
// gives real headroom, so a single glitch costs one audible gap instead of permanent lag.
//
// Returns 0 when the queue is within budget.
inline size_t audio_overflow_drop_bytes(size_t queued_bytes) {
  const size_t limit = audio_max_queue_bytes();
  if (queued_bytes <= limit) return 0;
  const size_t low_water = limit / 2;
  return queued_bytes - low_water;
}

}  // namespace droppix
