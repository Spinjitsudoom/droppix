package com.droppix.app.ui

/**
 * Pure aspect-fit math for GlDisplayView's video quad. No Android imports, so it can be
 * unit-tested directly (Robolectric-free) unlike the GL drawing code that consumes it.
 */
object AspectFit {
    /**
     * The clip-space half-extents (sx, sy) that scale a -1..1 quad down on exactly one axis
     * so the video is letterboxed (bars top/bottom) or pillarboxed (bars left/right) rather
     * than non-uniformly stretched to fill the view when the video's aspect ratio doesn't
     * exactly match the view's on-screen pixel aspect (e.g. evdi/CVT mode-timing rounding,
     * or the negotiated resolution not matching the view exactly).
     *
     * Falls back to (1f, 1f) — fullscreen, no bars — when either size is not yet known
     * (before the first CONFIG, or before the view has been measured) or non-positive.
     */
    fun scale(videoW: Int, videoH: Int, viewW: Int, viewH: Int): Pair<Float, Float> {
        if (videoW <= 0 || videoH <= 0 || viewW <= 0 || viewH <= 0) return 1f to 1f
        val videoAspect = videoW.toFloat() / videoH.toFloat()
        val viewAspect = viewW.toFloat() / viewH.toFloat()
        return when {
            videoAspect > viewAspect -> 1f to (viewAspect / videoAspect)   // letterbox: bars top/bottom
            videoAspect < viewAspect -> (videoAspect / viewAspect) to 1f   // pillarbox: bars left/right
            else -> 1f to 1f
        }
    }

    /**
     * A point on the view, along one axis, as 0..65535 across the picture actually drawn there.
     *
     * The picture fills the middle [extent] (the half-extent from [scale]) of the view's
     * [viewLen] pixels, so a touch has to skip the bars or it lands short of where the finger
     * is: up to a bar's width off at the edges. A touch on a bar clamps to the picture's edge.
     * [mirror] follows a horizontally flipped picture, so the touch lands on what is under the
     * finger.
     */
    fun toStream(px: Float, viewLen: Int, extent: Float, mirror: Boolean): Int {
        if (viewLen <= 0 || extent <= 0f) return 0
        var u = ((px / viewLen - 0.5f) / extent + 0.5f).coerceIn(0f, 1f)
        if (mirror) u = 1f - u
        return (u * 65535f).toInt()
    }
}
