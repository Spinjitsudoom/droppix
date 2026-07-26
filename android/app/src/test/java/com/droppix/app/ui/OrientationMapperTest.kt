package com.droppix.app.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class OrientationMapperTest {
    @Test fun startsAtLandscape() {
        assertEquals(0, OrientationMapper().currentCode())
    }

    @Test fun emitsAfterSettleNotBefore() {
        val m = OrientationMapper(settleMs = 250)
        assertNull(m.update(90, 0))      // candidate starts
        assertNull(m.update(90, 100))    // held 100ms < 250ms
        assertEquals(3, m.update(90, 300))  // settled (90° -> code 3)
        assertEquals(3, m.currentCode())
    }

    @Test fun noReEmitWhileStable() {
        val m = OrientationMapper(settleMs = 250)
        m.update(90, 0); m.update(90, 300)         // -> code 3 (90° portrait)
        assertNull(m.update(90, 600))               // already there, no re-emit
    }

    @Test fun deadZoneHoldsNearBoundary() {
        val m = OrientationMapper(deadZoneDeg = 12, settleMs = 0)
        // 45° is exactly on the 0<->90 boundary -> ignored, stays landscape (0).
        assertNull(m.update(45, 0))
        assertNull(m.update(45, 1000))
        assertEquals(0, m.currentCode())
    }

    @Test fun allFourOrientations() {
        val m = OrientationMapper(settleMs = 0)   // settle disabled for a clean sweep
        assertEquals(3, m.update(90, 0))   // 90° (portrait) -> code 3
        assertEquals(2, m.update(180, 0))  // 180° (upside landscape) -> code 2
        assertEquals(1, m.update(270, 0))  // 270° (reverse portrait) -> code 1
        assertEquals(0, m.update(0, 0))    // 0° (landscape) -> code 0
    }

    @Test fun unknownAngleIgnored() {
        val m = OrientationMapper(settleMs = 0)
        assertNull(m.update(-1, 0))     // ORIENTATION_UNKNOWN (flat)
        assertEquals(0, m.currentCode())
    }

    @Test fun candidateResetInterruptsSettle() {
        val m = OrientationMapper(settleMs = 250)
        assertNull(m.update(90, 0))     // candidate = portrait
        assertNull(m.update(180, 100))  // candidate switches -> timer restarts
        assertNull(m.update(180, 300))  // only 200ms into the new candidate
        assertEquals(2, m.update(180, 360))  // now settled at 180
    }
}
