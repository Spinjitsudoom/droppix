package com.droppix.app.settings

import android.content.Context

// Per-device display prefs the Android client sends to the host in HELLO v4. width/height == 0
// means "use this device's native screen resolution" (resolved at connect time). Rotation is
// NOT here — Android keeps its sensor auto-rotate.
data class AppSettings(
    val width: Int = 0, val height: Int = 0, val fps: Int = 60, val audio: Boolean = false,
    val bitrateKbps: Int = 8000, val rotationLocked: Boolean = false, val showOverlay: Boolean = false,
    val flipHorizontal: Boolean = false, val brightness: Int = 0, val contrast: Int = 100,
    val wallCol: Int = 0, val wallRow: Int = 0)

object Resolutions {
    // Presets are a TARGET HEIGHT, not a fixed pair. Fixed 16:9 pairs (1280x720 etc.) make the
    // host render the wrong SHAPE on a device that is not 16:9 — a 2340x1080 phone is 19.5:9 —
    // and the picture then has to be letterboxed or stretched to fit, so it never looks right
    // at any quality. Deriving the width from the device's own aspect makes the stream fill the
    // screen exactly.
    val PRESET_HEIGHTS: List<Int> = listOf(1440, 1080, 720, 540, 480)

    // The host expects landscape dims (orientation code drives the portrait swap).
    fun landscape(realW: Int, realH: Int): Pair<Int, Int> =
        if (realW >= realH) realW to realH else realH to realW

    // Scale to `targetH` while keeping the device's aspect ratio. Never upscales past the
    // panel (more pixels than the screen has only costs bandwidth), and keeps both dimensions
    // even because H.264 requires it.
    fun forHeight(targetH: Int, realW: Int, realH: Int): Pair<Int, Int> {
        val (w, h) = landscape(realW, realH)
        if (w < 2 || h < 2 || targetH < 2 || targetH >= h) return w to h
        val scaledW = Math.round(targetH * (w.toDouble() / h)).toInt()
        return maxOf(2, scaledW - (scaledW % 2)) to maxOf(2, targetH - (targetH % 2))
    }

    // The (w,h) to send in HELLO. A stored setting is honoured by its HEIGHT and re-derived to
    // the real aspect, so settings saved by an older build (which stored fixed 16:9 pairs) do
    // not resurrect the wrong shape.
    fun resolve(s: AppSettings, realW: Int, realH: Int): Pair<Int, Int> =
        if (s.height > 0) forHeight(s.height, realW, realH) else landscape(realW, realH)
}

class SettingsStore(context: Context) {
    private val prefs = context.getSharedPreferences("droppix", Context.MODE_PRIVATE)
    fun load(): AppSettings = AppSettings(
        width = prefs.getInt("res_w", 0), height = prefs.getInt("res_h", 0),
        fps = prefs.getInt("fps", 60), audio = prefs.getBoolean("audio", false),
        bitrateKbps = prefs.getInt("bitrate", 8000), rotationLocked = prefs.getBoolean("rot_lock", false),
        showOverlay = prefs.getBoolean("overlay", false), flipHorizontal = prefs.getBoolean("flip_h", false),
        brightness = prefs.getInt("brightness", 0), contrast = prefs.getInt("contrast", 100),
        wallCol = prefs.getInt("wall_col", 0), wallRow = prefs.getInt("wall_row", 0))
    fun save(s: AppSettings) = prefs.edit()
        .putInt("res_w", s.width).putInt("res_h", s.height)
        .putInt("fps", s.fps).putBoolean("audio", s.audio)
        .putInt("bitrate", s.bitrateKbps).putBoolean("rot_lock", s.rotationLocked).putBoolean("overlay", s.showOverlay).putBoolean("flip_h", s.flipHorizontal)
        .putInt("brightness", s.brightness).putInt("contrast", s.contrast)
        .putInt("wall_col", s.wallCol).putInt("wall_row", s.wallRow).apply()
}
