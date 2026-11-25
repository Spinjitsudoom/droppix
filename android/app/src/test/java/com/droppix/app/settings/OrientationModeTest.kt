package com.droppix.app.settings

import com.droppix.app.ui.OrientationMapper
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

// A manual orientation choice (user request 2026-11-25): when Android's own sensor gets the
// tablet's orientation wrong, a fixed choice forces both the tablet's display and the shape the
// host builds.
class OrientationModeTest {
    private fun isPortraitCode(code: Int) = code == 1 || code == 3

    private fun portraitFor(mode: OrientationMode, naturalIsPortrait: Boolean): Boolean {
        val m = OrientationMapper(naturalIsPortrait = naturalIsPortrait)
        m.seedFromDisplayRotation(mode.displayRotation(naturalIsPortrait)!!)
        return isPortraitCode(m.currentCode())
    }

    @Test fun fixedModesGiveTheirShapeOnATablet() {
        assertEquals(false, portraitFor(OrientationMode.LANDSCAPE, naturalIsPortrait = false))
        assertEquals(false, portraitFor(OrientationMode.LANDSCAPE_REVERSED, naturalIsPortrait = false))
        assertEquals(true, portraitFor(OrientationMode.PORTRAIT, naturalIsPortrait = false))
        assertEquals(true, portraitFor(OrientationMode.PORTRAIT_REVERSED, naturalIsPortrait = false))
    }

    @Test fun fixedModesGiveTheirShapeOnAPhone() {
        assertEquals(false, portraitFor(OrientationMode.LANDSCAPE, naturalIsPortrait = true))
        assertEquals(false, portraitFor(OrientationMode.LANDSCAPE_REVERSED, naturalIsPortrait = true))
        assertEquals(true, portraitFor(OrientationMode.PORTRAIT, naturalIsPortrait = true))
        assertEquals(true, portraitFor(OrientationMode.PORTRAIT_REVERSED, naturalIsPortrait = true))
    }

    @Test fun reversedIsTheOppositeRotationOfItsShape() {
        for (natural in listOf(false, true)) {
            val l = OrientationMode.LANDSCAPE.displayRotation(natural)!!
            assertEquals((l + 2) % 4, OrientationMode.LANDSCAPE_REVERSED.displayRotation(natural))
            val p = OrientationMode.PORTRAIT.displayRotation(natural)!!
            assertEquals((p + 2) % 4, OrientationMode.PORTRAIT_REVERSED.displayRotation(natural))
        }
    }

    @Test fun autoAndLockedFollowTheDevice() {
        assertNull(OrientationMode.AUTO.displayRotation(false))
        assertNull(OrientationMode.LOCKED.displayRotation(true))
    }

    @Test fun storedValueAndTheOldLockSwitch() {
        assertEquals(OrientationMode.AUTO, OrientationMode.fromPref(null, legacyLocked = false))
        assertEquals(OrientationMode.LOCKED, OrientationMode.fromPref(null, legacyLocked = true))   // old "Lock rotation" on
        assertEquals(OrientationMode.PORTRAIT, OrientationMode.fromPref("PORTRAIT", legacyLocked = false))
        assertEquals(OrientationMode.AUTO, OrientationMode.fromPref("nonsense", legacyLocked = true))
    }

    @Test fun everyModeHasALabel() {
        assertEquals(6, OrientationMode.entries.size)
        OrientationMode.entries.forEach { assert(it.label.isNotBlank()) }
    }
}
