package com.droppix.app.ui

import com.droppix.app.settings.AppSettings
import com.droppix.app.settings.OrientationMode
import org.junit.Assert.assertEquals
import org.junit.Test

class PanelChangeTest {
    private val base = AppSettings(height = 480, fps = 60, bitrateKbps = 8000)

    @Test fun sameSettingsIsNoChange() {
        // Android delivers a spinner's onItemSelected AFTER setSelection returns, so seeding
        // the panel fires listeners with the values it already holds. Those must do nothing:
        // the resolution one used to restart the stream every time the panel first opened.
        assertEquals(PanelChange.NONE, PanelChange.of(base, base.copy()))
    }

    @Test fun displayOnlyChanges() {
        assertEquals(PanelChange.DISPLAY, PanelChange.of(base, base.copy(flipHorizontal = true)))
        assertEquals(PanelChange.DISPLAY, PanelChange.of(base, base.copy(brightness = 10)))
        assertEquals(PanelChange.DISPLAY, PanelChange.of(base, base.copy(orientation = OrientationMode.PORTRAIT)))
    }

    @Test fun streamParamsApplyLive() {
        assertEquals(PanelChange.STREAM_PARAMS, PanelChange.of(base, base.copy(fps = 30)))
        assertEquals(PanelChange.STREAM_PARAMS, PanelChange.of(base, base.copy(bitrateKbps = 16000)))
        assertEquals(PanelChange.STREAM_PARAMS, PanelChange.of(base, base.copy(audio = true)))
    }

    @Test fun resolutionReconnectsAndWinsOverTheRest() {
        assertEquals(PanelChange.RESOLUTION, PanelChange.of(base, base.copy(height = 1080)))
        assertEquals(PanelChange.RESOLUTION, PanelChange.of(base, base.copy(height = 0, fps = 30)))
    }
}
