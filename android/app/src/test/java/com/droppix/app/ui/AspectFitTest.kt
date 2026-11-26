package com.droppix.app.ui

import org.junit.Assert.assertEquals
import org.junit.Test

class AspectFitTest {
    private val eps = 1e-4f

    @Test fun exactMatchIsFullscreen() {
        val (sx, sy) = AspectFit.scale(1920, 1080, 1920, 1080)
        assertEquals(1f, sx, eps); assertEquals(1f, sy, eps)
    }

    @Test fun sameAspectDifferentSizeIsFullscreen() {
        // 1280x720 and 1920x1080 are both exactly 16:9 -- no bars needed even though the
        // absolute pixel sizes differ.
        val (sx, sy) = AspectFit.scale(1280, 720, 1920, 1080)
        assertEquals(1f, sx, eps); assertEquals(1f, sy, eps)
    }

    @Test fun viewWiderThanVideoPillarboxes() {
        // A modern phone's native landscape (2340x1080, ~2.17:1) is wider than the evdi mode
        // the host actually managed to create (1920x1080, 16:9) -- bars go on left/right,
        // full height, reduced width, matching the ratio of the two aspect ratios.
        val (sx, sy) = AspectFit.scale(1920, 1080, 2340, 1080)
        assertEquals(1920f / 2340f, sx, eps)   // videoAspect / viewAspect, both heights equal here
        assertEquals(1f, sy, eps)
    }

    @Test fun videoWiderThanViewLetterboxes() {
        // The reverse: video wider than the view -- bars go on top/bottom, full width.
        val (sx, sy) = AspectFit.scale(2340, 1080, 1920, 1080)
        assertEquals(1f, sx, eps)
        assertEquals(1920f / 2340f, sy, eps)   // viewAspect / videoAspect, both heights equal here
    }

    @Test fun zeroVideoSizeFallsBackToFullscreen() {
        val (sx, sy) = AspectFit.scale(0, 0, 1920, 1080)
        assertEquals(1f, sx, eps); assertEquals(1f, sy, eps)
    }

    @Test fun zeroViewSizeFallsBackToFullscreen() {
        val (sx, sy) = AspectFit.scale(1920, 1080, 0, 0)
        assertEquals(1f, sx, eps); assertEquals(1f, sy, eps)
    }

    @Test fun negativeSizeFallsBackToFullscreen() {
        val (sx, sy) = AspectFit.scale(-1, 1080, 1920, 1080)
        assertEquals(1f, sx, eps); assertEquals(1f, sy, eps)
    }

    @Test fun portraitVideoAndViewExactMatch() {
        // Portrait-shaped session (orientation swap): still just aspect comparison, no special-casing.
        val (sx, sy) = AspectFit.scale(1080, 2340, 1080, 2340)
        assertEquals(1f, sx, eps); assertEquals(1f, sy, eps)
    }

    // ---- toStream: a touch on the view -> 0..65535 across the picture actually drawn ----

    @Test fun touchMapsAcrossTheWholeViewWhenThereAreNoBars() {
        assertEquals(0, AspectFit.toStream(0f, 2560, 1f, false))
        assertEquals(32767, AspectFit.toStream(1280f, 2560, 1f, false))
        assertEquals(65535, AspectFit.toStream(2560f, 2560, 1f, false))
    }

    @Test fun touchSkipsThePillarboxBars() {
        // 768x480 (1.6) in a 2560x1504 view (nav bar showing): the picture is 2406 px wide,
        // centred, with 77 px bars. 10% across the PICTURE is 77 + 240.6 = 317.6 view px.
        val (sx, _) = AspectFit.scale(768, 480, 2560, 1504)
        assertEquals(6553f, AspectFit.toStream(317.6f, 2560, sx, false).toFloat(), 10f)
        assertEquals(32767, AspectFit.toStream(1280f, 2560, sx, false))
    }

    @Test fun touchOnABarClampsToThePictureEdge() {
        val (sx, _) = AspectFit.scale(768, 480, 2560, 1504)
        assertEquals(0, AspectFit.toStream(20f, 2560, sx, false))
        assertEquals(65535, AspectFit.toStream(2550f, 2560, sx, false))
    }

    @Test fun flippedPictureMirrorsTheTouch() {
        assertEquals(65535, AspectFit.toStream(0f, 2560, 1f, true))
        assertEquals(6553f, AspectFit.toStream(2304f, 2560, 1f, true).toFloat(), 2f)
    }

    @Test fun unmeasuredViewDoesNotDivideByZero() {
        assertEquals(0, AspectFit.toStream(5f, 0, 1f, false))
    }
}
