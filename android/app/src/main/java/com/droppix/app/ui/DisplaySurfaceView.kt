package com.droppix.app.ui

import android.content.Context
import android.util.AttributeSet
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView

class DisplaySurfaceView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null
) : SurfaceView(context, attrs), SurfaceHolder.Callback {

    interface SurfaceListener {
        fun onSurfaceReady(surface: Surface)
        fun onSurfaceGone()
    }

    private var listener: SurfaceListener? = null

    init { holder.addCallback(this) }

    // Register (or clear with null) the lifecycle listener. If the surface is
    // already valid, onSurfaceReady fires immediately.
    fun setSurfaceListener(l: SurfaceListener?) {
        listener = l
        val s = holder.surface
        if (l != null && s != null && s.isValid) l.onSurfaceReady(s)
    }

    override fun surfaceCreated(h: SurfaceHolder) { listener?.onSurfaceReady(h.surface) }
    override fun surfaceChanged(h: SurfaceHolder, f: Int, w: Int, ht: Int) {}
    override fun surfaceDestroyed(h: SurfaceHolder) { listener?.onSurfaceGone() }
}
