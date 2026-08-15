/**
 * When to paint a decoded frame.
 *
 * droppix streams a LIVE desktop, but the client was presenting it like a video player:
 * it buffered up to 12 decoded frames and held every one until the audio media clock
 * caught up. On a desktop that is felt directly — the pointer lags by however deep the
 * audio buffer is — and it is the most likely reason spacedesk's viewer feels smoother
 * despite a far less efficient codec (it simply paints on arrival).
 *
 * Lipsync still matters if you play a video on the extended screen, so the clock is not
 * discarded: it is BOUNDED. We sync to audio while that is cheap, and stop waiting once
 * doing so would cost more latency than the sync is worth.
 *
 * Both frames and clock are in microseconds (stream PTS).
 */

/** Decoded frames held for presentation. Deep queues are pure latency on a live stream. */
export const kMaxPendingFrames = 3;

/**
 * How far video may lead the audio clock before we stop waiting for audio. ~1.5 frames at
 * 60fps: enough that normal jitter still syncs, small enough that a stalled or
 * deeply-buffered audio path cannot hold the desktop hostage.
 */
export const kMaxVideoLeadUs = 25_000;

/**
 * Index of the frame to paint from `timestamps` (ascending), or -1 to wait.
 *
 * - no clock            → newest (paint on arrival)
 * - a frame is due      → newest frame at or behind the clock; older ones are stale
 * - all frames ahead    → wait, UNLESS the lead exceeds kMaxVideoLeadUs, in which case
 *                         audio is too far behind to sync to and we paint the newest
 */
export function choosePaintIndex(
  timestamps: readonly number[],
  clock: number | null,
  maxLeadUs: number = kMaxVideoLeadUs,
): number {
  if (timestamps.length === 0) return -1;
  if (clock == null) return timestamps.length - 1;

  let due = -1;
  for (let i = 0; i < timestamps.length; i++) {
    if (timestamps[i]! <= clock) due = i;
  }
  if (due >= 0) return due;

  // Everything is ahead of the clock: audio has not reached this frame yet.
  const lead = timestamps[0]! - clock;
  return lead > maxLeadUs ? timestamps.length - 1 : -1;
}
