package com.droppix.app.net

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

// On the real Nexus 10 a USB session ended, the app retried openAccessory 20 times 200 ms apart
// (4 s), gave up, and left the stream screen while the PC was still re-running its accessory
// handshake — then sat on the connect screen for good. The retry decision is now: keep going
// while a droppix accessory is attached; give up only on a real unplug or a long dead spell.
class AoaRetryTest {
    @Test fun keepsRetryingWhileTheAccessoryIsAttached() {
        val r = AoaRetry(); r.reset(0)
        assertTrue(r.shouldContinue(4_000, accessoryAttached = true))    // old code gave up here
        assertTrue(r.shouldContinue(60_000, accessoryAttached = true))
    }
    @Test fun givesUpOnAnUnplugAfterTheGrace() {
        val r = AoaRetry(); r.reset(0)
        assertTrue(r.shouldContinue(1_000, accessoryAttached = false))   // re-enumerating
        assertTrue(r.shouldContinue(3_500, accessoryAttached = false))
        assertFalse(r.shouldContinue(4_100, accessoryAttached = false))  // gone 3 s+: unplugged
    }
    @Test fun reattachingRestartsTheUnplugClock() {
        val r = AoaRetry(); r.reset(0)
        r.shouldContinue(1_000, accessoryAttached = false)
        r.shouldContinue(2_000, accessoryAttached = true)                 // came back
        assertTrue(r.shouldContinue(4_500, accessoryAttached = false))   // new gap, only 2.5 s
    }
    @Test fun givesUpAfterALongSpellWithNoVideo() {
        val r = AoaRetry(); r.reset(0)
        assertFalse(r.shouldContinue(121_000, accessoryAttached = true))
    }
    @Test fun aWorkingSessionResetsTheBudget() {
        val r = AoaRetry(); r.reset(0)
        r.reset(100_000)                                                  // video flowed
        assertTrue(r.shouldContinue(200_000, accessoryAttached = true))
    }
}
