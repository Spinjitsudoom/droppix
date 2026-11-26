package com.droppix.app.net

/**
 * When to keep reopening the USB accessory after a session ends. Pure (unit-tested).
 *
 * The PC re-runs its accessory handshake after a session ends, which re-enumerates the USB link:
 * for a few seconds the accessory is missing or can't be opened. A fixed 4 s budget gave up in the
 * middle of that and stranded the tablet on the connect screen. Now: keep going while a droppix
 * accessory is attached; stop only once it has been gone for [unplugGraceMs] (a real unplug) or
 * nothing has worked for [maxNoVideoMs].
 */
class AoaRetry(private val unplugGraceMs: Long = 3_000, private val maxNoVideoMs: Long = 120_000) {
    private var budgetStartMs = 0L
    private var detachedSinceMs = -1L

    /** A session delivered video: start a fresh budget. */
    fun reset(nowMs: Long) { budgetStartMs = nowMs; detachedSinceMs = -1L }

    fun shouldContinue(nowMs: Long, accessoryAttached: Boolean): Boolean {
        if (nowMs - budgetStartMs >= maxNoVideoMs) return false
        if (accessoryAttached) { detachedSinceMs = -1L; return true }
        if (detachedSinceMs < 0) detachedSinceMs = nowMs
        return nowMs - detachedSinceMs < unplugGraceMs
    }
}
