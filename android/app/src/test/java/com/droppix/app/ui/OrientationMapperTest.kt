package com.droppix.app.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
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

    // ---- natural-orientation handling ------------------------------------------------
    //
    // OrientationEventListener reports rotation away from the device's NATURAL orientation,
    // so angle 0 is portrait on a phone and landscape on a tablet. Getting this wrong
    // inverted every orientation: a phone held in portrait made the host render landscape.
    // Host convention (orientation.h): codes 1 and 3 are portrait, 0 and 2 landscape.

    private fun isPortraitCode(code: Int) = code == 1 || code == 3

    @Test fun phonePortraitAsksForAPortraitDisplay() {
        val m = OrientationMapper(naturalIsPortrait = true)
        m.update(0, 0L); m.update(0, 1000L)          // natural = portrait on a phone
        assertTrue("phone held portrait must request a portrait display",
            isPortraitCode(m.currentCode()))
    }

    @Test fun phoneLandscapeAsksForALandscapeDisplay() {
        val m = OrientationMapper(naturalIsPortrait = true)
        m.update(90, 0L); m.update(90, 1000L)
        assertFalse("phone turned landscape must request a landscape display",
            isPortraitCode(m.currentCode()))
    }

    @Test fun everyPhoneQuarterMapsToTheMatchingShape() {
        // 0 and 180 are portrait on a phone; 90 and 270 are landscape.
        for ((angle, wantPortrait) in listOf(0 to true, 90 to false, 180 to true, 270 to false)) {
            val m = OrientationMapper(naturalIsPortrait = true)
            m.update(angle, 0L); m.update(angle, 1000L)
            assertEquals("angle $angle on a phone", wantPortrait, isPortraitCode(m.currentCode()))
        }
    }

    @Test fun tabletBehaviourIsUnchanged() {
        // A landscape-natural device must keep working exactly as before this fix.
        for ((angle, wantPortrait) in listOf(0 to false, 90 to true, 180 to false, 270 to true)) {
            val m = OrientationMapper(naturalIsPortrait = false)
            m.update(angle, 0L); m.update(angle, 1000L)
            assertEquals("angle $angle on a tablet", wantPortrait, isPortraitCode(m.currentCode()))
        }
    }

    @Test fun phoneAndTabletDisagreeOnTheSameAngle() {
        // The whole point: identical sensor input, opposite shapes.
        val phone = OrientationMapper(naturalIsPortrait = true)
        val tablet = OrientationMapper(naturalIsPortrait = false)
        phone.update(0, 0L); phone.update(0, 1000L)
        tablet.update(0, 0L); tablet.update(0, 1000L)
        assertNotEquals(isPortraitCode(phone.currentCode()), isPortraitCode(tablet.currentCode()))
    }
}
