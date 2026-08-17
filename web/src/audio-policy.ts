// How far ahead of the audio clock we are willing to schedule.
//
// The scheduler advances `nextTime` by each buffer's duration, so it tracks how much audio is
// booked but not yet heard — i.e. latency. It only ever had a LOWER bound (bump it forward on
// underrun). With no upper bound, any burst from the host — a stall in the stream loop, a
// slow link catching up — pushed `nextTime` further ahead and it never came back: playback
// advances in real time, so being early is permanent until something throws audio away.
//
// This is the same discipline `paint-policy.ts` applies to video: cap the lead, and when it
// is exceeded, resync to now rather than politely queueing more.
export const kMaxAudioLeadSec = 0.25;

/** Where to resync to when the lead is blown. Enough to survive normal jitter, no more. */
export const kAudioResyncLeadSec = 0.06;

/** Minimum lead below which we are at risk of underrunning and should schedule immediately. */
export const kAudioMinLeadSec = 0.03;

export interface AudioSchedule {
  /** Absolute context time to start this buffer at. */
  startAt: number;
  /** True when we gave up on the old schedule and jumped back to the present. */
  resynced: boolean;
}

/**
 * Decide when the next buffer should start.
 *
 * `nextTime` is the running schedule cursor, `now` the AudioContext clock.
 *
 * - Too far ahead (> kMaxAudioLeadSec): we are audibly behind the host. Drop the schedule and
 *   restart near the present — one gap, then correct timing.
 * - Too close to now (underrun): nudge forward so the buffer is not scheduled in the past,
 *   which the Web Audio API would otherwise play immediately and overlapped.
 * - Otherwise: continue the existing schedule, which is what keeps playback gapless.
 */
export function scheduleAudio(nextTime: number, now: number): AudioSchedule {
  const lead = nextTime - now;
  if (lead > kMaxAudioLeadSec) {
    return { startAt: now + kAudioResyncLeadSec, resynced: true };
  }
  if (lead < kAudioMinLeadSec) {
    return { startAt: now + kAudioMinLeadSec, resynced: false };
  }
  return { startAt: nextTime, resynced: false };
}
