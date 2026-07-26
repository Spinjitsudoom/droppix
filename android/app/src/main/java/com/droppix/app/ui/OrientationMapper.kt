package com.droppix.app.ui

import kotlin.math.abs

/**
 * Buckets the device's physical orientation (the angle from Android's
 * OrientationEventListener) into one of four ORIENTATION wire codes, and emits a
 * code only when the device has *settled* into a new orientation.
 *
 * Anti-jitter is two-fold:
 *  - a spatial dead-zone around the 45° quarter boundaries (so holding near a
 *    boundary doesn't flip-flop), and
 *  - a settle time (the new quarter must be held for [settleMs] before emitting).
 *
 * Pure logic (no Android imports) so it can be unit-tested with synthetic angle /
 * timestamp sequences.
 */
class OrientationMapper(
    private val deadZoneDeg: Int = 12,
    private val settleMs: Long = 250,
) {
    @Volatile private var currentQuarter = 0   // last emitted quarter (0..3)
    private var candidateQuarter = 0
    private var candidateSinceMs = 0L

    /** ORIENTATION code for the current settled orientation (0/1/2/3). */
    fun currentCode(): Int = QUARTER_TO_CODE[currentQuarter]

    /**
     * Feed one sensor sample. [angleDeg] is 0..359, or <0 for ORIENTATION_UNKNOWN
     * (device flat) which is ignored. Returns the new ORIENTATION code if the device
     * just settled into a new orientation, else null.
     */
    fun update(angleDeg: Int, nowMs: Long): Int? {
        if (angleDeg < 0) return null                       // flat / unknown: ignore
        val quarter = ((angleDeg + 45) / 90) % 4
        // Dead-zone: ignore samples too close to a quarter boundary.
        var offset = abs(angleDeg - quarter * 90)
        if (offset > 180) offset = 360 - offset
        if (offset > 45 - deadZoneDeg) return null

        if (quarter == currentQuarter) { candidateQuarter = quarter; return null }
        if (quarter != candidateQuarter) {                  // new candidate: start settle timer
            candidateQuarter = quarter
            candidateSinceMs = nowMs
        }
        if (nowMs - candidateSinceMs < settleMs) return null  // not held long enough yet
        currentQuarter = quarter
        return QUARTER_TO_CODE[quarter]
    }

    companion object {
        // Physical quarter -> ORIENTATION code (host KWin rotation). Identity default;
        // the 90°/270° direction is the on-device calibration constant — swap indices
        // 1 and 3 here if rotating the tablet turns the desktop the wrong way.
        private val QUARTER_TO_CODE = intArrayOf(0, 3, 2, 1)
    }
}
