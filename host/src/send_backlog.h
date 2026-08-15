#pragma once
#include <algorithm>
#include <cstddef>

namespace droppix {

// Source-side pacing against a slow link.
//
// The encoder runs at a fixed bitrate; if the client's link can't carry it, the excess
// does NOT surface as slow writes (the kernel buffers absorb it). It accumulates as an
// unacknowledged send backlog: latency grows without bound and the picture arrives late
// and stuttering, while the host still measures a perfectly healthy send time.
//
// The cure is to stop producing frames the link cannot carry. Frames must be dropped
// BEFORE the encoder sees them: a skipped capture leaves the encoder's reference chain
// intact (the next frame is simply a delta against the last *encoded* one), whereas
// discarding encoded packets breaks that chain and corrupts the picture until the next
// keyframe. Result: fewer frames, bounded latency, no corruption.

// Backlog (bytes) above which we stop feeding the encoder, derived from the session
// bitrate so it scales with stream size. ~250 ms of data, with a floor for tiny bitrates
// (a threshold under one frame would drop constantly on normal jitter).
inline size_t backlog_limit_bytes(int bitrate_kbps) {
  const size_t quarter_second = static_cast<size_t>(std::max(0, bitrate_kbps)) * 1000 / 8 / 4;
  return std::max<size_t>(64u * 1024u, quarter_second);
}

// Should this capture be skipped? Hysteresis: once dropping starts, keep dropping until
// the backlog falls to half the limit, so we don't oscillate on the boundary.
inline bool should_skip_frame(size_t pending, size_t limit, bool currently_dropping) {
  if (currently_dropping) return pending > limit / 2;
  return pending > limit;
}

}  // namespace droppix
