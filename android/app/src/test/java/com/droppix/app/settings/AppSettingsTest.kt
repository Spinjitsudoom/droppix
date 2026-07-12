package com.droppix.app.settings
import org.junit.Assert.*
import org.junit.Test
class AppSettingsTest {
    @Test fun landscapeNormalizesToWidthGeHeight() {
        assertEquals(2400 to 1080, Resolutions.landscape(1080, 2400))   // portrait device -> landscape
        assertEquals(2560 to 1600, Resolutions.landscape(2560, 1600))   // already landscape
    }
    @Test fun resolveUsesNativeWhenUnset() {
        assertEquals(2400 to 1080, Resolutions.resolve(AppSettings(), 1080, 2400))  // width==0 -> native
    }
    @Test fun resolveUsesExplicitWhenSet() {
        assertEquals(1280 to 720, Resolutions.resolve(AppSettings(width = 1280, height = 720), 1080, 2400))
    }
    @Test fun defaults() {
        val s = AppSettings()
        assertEquals(0, s.width); assertEquals(60, s.fps); assertFalse(s.audio)
    }
    @Test fun newDefaults() {
        val s = AppSettings()
        assertEquals(8000, s.bitrateKbps); assertFalse(s.rotationLocked); assertFalse(s.showOverlay)
    }
}
