package com.droppix.app.ui

import com.droppix.app.settings.AppSettings

/**
 * What a change in the in-stream settings panel has to do, from the settings before and after.
 *
 * Deciding from the values, rather than from which control fired, is what makes the panel safe
 * against Android's spinners: they report a selection AFTER setSelection returns, so filling
 * the panel in fires their listeners with the values it already shows. Those come out NONE.
 */
enum class PanelChange {
    NONE,
    /** Local to the tablet's view (flip, brightness, contrast, overlay, orientation). */
    DISPLAY,
    /** fps / quality / audio: pushed to the running session. */
    STREAM_PARAMS,
    /** The PC must re-create its virtual screen, so the session restarts. */
    RESOLUTION;

    companion object {
        fun of(before: AppSettings, after: AppSettings): PanelChange = when {
            // Only the target height is chosen here; the width is derived at connect time.
            after.height != before.height -> RESOLUTION
            after.fps != before.fps || after.bitrateKbps != before.bitrateKbps ||
                after.audio != before.audio -> STREAM_PARAMS
            after != before.copy(width = after.width) -> DISPLAY
            else -> NONE
        }
    }
}
