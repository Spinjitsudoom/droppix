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
    @Test fun resolveKeepsTheDeviceAspectRatio() {
        // A stored preset is honoured by its HEIGHT and the width re-derived from the panel,
        // so the host renders the device's SHAPE. A fixed 1280x720 (16:9) on a 2400x1080
        // (20:9) device made the host render the wrong shape, which then had to be
        // letterboxed or stretched — it never looked right at any quality.
        val (w, h) = Resolutions.resolve(AppSettings(width = 1280, height = 720), 1080, 2400)
        assertEquals(720, h)
        assertEquals(1600, w)                        // 720 * (2400/1080)
        assertEquals(2400.0 / 1080.0, w.toDouble() / h, 0.01)
    }

    @Test fun resolveNeverUpscalesPastThePanel() {
        assertEquals(2400 to 1080, Resolutions.resolve(AppSettings(height = 1440), 1080, 2400))
    }

    @Test fun resolveKeepsDimensionsEven() {
        // H.264 requires even width/height.
        val (w, h) = Resolutions.resolve(AppSettings(height = 541), 1080, 2337)
        assertEquals(0, w % 2); assertEquals(0, h % 2)
    }
    @Test fun defaults() {
        val s = AppSettings()
        assertEquals(0, s.width); assertEquals(60, s.fps); assertFalse(s.audio)
    }
    @Test fun newDefaults() {
        val s = AppSettings()
        assertEquals(8000, s.bitrateKbps); assertFalse(s.rotationLocked); assertFalse(s.showOverlay)
    }
    @Test fun flipDefault() { assertFalse(AppSettings().flipHorizontal) }
    @Test fun brightnessContrastDefaults() {
        val s = AppSettings()
        assertEquals(0, s.brightness); assertEquals(100, s.contrast)
    }
    @Test fun wallDefaults() {
        val s = AppSettings()
        assertEquals(0, s.wallCol); assertEquals(0, s.wallRow)
    }
}
