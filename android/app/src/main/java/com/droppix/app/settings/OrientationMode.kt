package com.droppix.app.settings

/**
 * How the stream's orientation is chosen. AUTO follows the device's sensor; LOCKED keeps
 * whatever the display is at; the four fixed modes force a shape, for when Android's own
 * sensor reads the tablet wrong (a stand at an angle, a flaky accelerometer).
 *
 * Pure: no Android imports, so it is unit-tested.
 */
enum class OrientationMode(val label: String, val activityOrientation: Int) {
    // activityOrientation = ActivityInfo.SCREEN_ORIENTATION_* (values inlined to stay pure)
    AUTO("Auto", 10),                                // FULL_SENSOR
    LOCKED("Locked", 14),                            // LOCKED: stay at the current rotation
    LANDSCAPE("Landscape", 0),                       // LANDSCAPE
    PORTRAIT("Portrait", 1),                         // PORTRAIT
    LANDSCAPE_REVERSED("Landscape (upside down)", 8), // REVERSE_LANDSCAPE
    PORTRAIT_REVERSED("Portrait (upside down)", 9);  // REVERSE_PORTRAIT

    /**
     * The display rotation (Surface.ROTATION_0..270 as 0..3) a fixed mode puts the device at,
     * or null for AUTO/LOCKED, which follow the device. Lets the stream report the chosen shape
     * at once, without waiting for Android to finish rotating.
     */
    fun displayRotation(naturalIsPortrait: Boolean): Int? = when (this) {
        AUTO, LOCKED -> null
        LANDSCAPE -> if (naturalIsPortrait) 1 else 0
        LANDSCAPE_REVERSED -> if (naturalIsPortrait) 3 else 2
        PORTRAIT -> if (naturalIsPortrait) 0 else 1
        PORTRAIT_REVERSED -> if (naturalIsPortrait) 2 else 3
    }

    val isFixed: Boolean get() = this != AUTO

    companion object {
        /** The stored choice; before this setting existed only "Lock rotation" was stored. */
        fun fromPref(name: String?, legacyLocked: Boolean): OrientationMode =
            if (name == null) (if (legacyLocked) LOCKED else AUTO)
            else entries.firstOrNull { it.name == name } ?: AUTO
    }
}
