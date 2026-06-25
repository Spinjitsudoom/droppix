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

    // Single-pointer touch, normalized to 0..65535 of the view, sent to the host.
    interface TouchListener { fun onTouch(action: Int, xNorm: Int, yNorm: Int) }

    private var listener: SurfaceListener? = null
    private var touchListener: TouchListener? = null

    init { holder.addCallback(this) }

    fun setTouchListener(l: TouchListener?) { touchListener = l }

    override fun onTouchEvent(event: android.view.MotionEvent): Boolean {
        val l = touchListener ?: return false
        val action = when (event.actionMasked) {
            android.view.MotionEvent.ACTION_DOWN -> 0
            android.view.MotionEvent.ACTION_MOVE -> 1
            android.view.MotionEvent.ACTION_UP, android.view.MotionEvent.ACTION_CANCEL -> 2
            else -> return false
        }
        val w = width.coerceAtLeast(1); val h = height.coerceAtLeast(1)
        val xn = ((event.x / w).coerceIn(0f, 1f) * 65535f).toInt()
        val yn = ((event.y / h).coerceIn(0f, 1f) * 65535f).toInt()
        l.onTouch(action, xn, yn)
        return true
    }

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
