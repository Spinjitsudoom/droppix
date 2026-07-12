package com.droppix.app.settings

import android.content.Context

// Per-device display prefs the Android client sends to the host in HELLO v4. width/height == 0
// means "use this device's native screen resolution" (resolved at connect time). Rotation is
// NOT here — Android keeps its sensor auto-rotate.
data class AppSettings(
    val width: Int = 0, val height: Int = 0, val fps: Int = 60, val audio: Boolean = false,
    val bitrateKbps: Int = 8000, val rotationLocked: Boolean = false, val showOverlay: Boolean = false,
    val flipHorizontal: Boolean = false)

object Resolutions {
    // Presets offered in the UI, in addition to "Native". Landscape-oriented (width >= height).
    val PRESETS: List<Pair<Int, Int>> = listOf(1280 to 720, 1920 to 1080, 2560 to 1440, 800 to 480)

    // The host expects landscape dims (orientation code drives the portrait swap).
    fun landscape(realW: Int, realH: Int): Pair<Int, Int> =
        if (realW >= realH) realW to realH else realH to realW

    // The (w,h) to send in HELLO: explicit setting when set, else the device's native (landscape).
    fun resolve(s: AppSettings, realW: Int, realH: Int): Pair<Int, Int> =
        if (s.width > 0 && s.height > 0) s.width to s.height else landscape(realW, realH)
}

class SettingsStore(context: Context) {
    private val prefs = context.getSharedPreferences("droppix", Context.MODE_PRIVATE)
    fun load(): AppSettings = AppSettings(
        width = prefs.getInt("res_w", 0), height = prefs.getInt("res_h", 0),
        fps = prefs.getInt("fps", 60), audio = prefs.getBoolean("audio", false),
        bitrateKbps = prefs.getInt("bitrate", 8000), rotationLocked = prefs.getBoolean("rot_lock", false),
        showOverlay = prefs.getBoolean("overlay", false), flipHorizontal = prefs.getBoolean("flip_h", false))
    fun save(s: AppSettings) = prefs.edit()
        .putInt("res_w", s.width).putInt("res_h", s.height)
        .putInt("fps", s.fps).putBoolean("audio", s.audio)
        .putInt("bitrate", s.bitrateKbps).putBoolean("rot_lock", s.rotationLocked).putBoolean("overlay", s.showOverlay).putBoolean("flip_h", s.flipHorizontal).apply()
}
