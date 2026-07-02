package com.droppix.app.ui

import android.content.Context
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import com.droppix.app.protocol.Contact

class DisplaySurfaceView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null
) : SurfaceView(context, attrs), SurfaceHolder.Callback {

    interface SurfaceListener {
        fun onSurfaceReady(surface: Surface)
        fun onSurfaceGone()
    }

    // Multi-touch: the full set of active contacts (each normalized to 0..65535 of the view),
    // sent to the host every event. An empty list means all fingers lifted.
    interface TouchListener { fun onTouch(contacts: List<Contact>) }

    private var listener: SurfaceListener? = null
    private var touchListener: TouchListener? = null
    private var lastMoveSentMs = 0L
    private val moveMinIntervalMs = 12L   // coalesce MOVEs to ~80 Hz max

    init { holder.addCallback(this) }

    fun setTouchListener(l: TouchListener?) { touchListener = l }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val l = touchListener ?: return false
        val masked = event.actionMasked
        when (masked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_MOVE,
            MotionEvent.ACTION_POINTER_DOWN, MotionEvent.ACTION_POINTER_UP,
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {}
            else -> return false
        }
        // Coalesce the high-rate MOVE stream so a drag can't flood the host; every finger
        // add/remove is always sent (a dropped up would leave a finger stuck down).
        if (masked == MotionEvent.ACTION_MOVE) {
            val now = System.currentTimeMillis()
            if (now - lastMoveSentMs < moveMinIntervalMs) return true
            lastMoveSentMs = now
        }
        val w = width.coerceAtLeast(1); val h = height.coerceAtLeast(1)
        val contacts = if (masked == MotionEvent.ACTION_CANCEL) {
            emptyList()
        } else {
            // On a finger lift, exclude the pointer that is going up from the active set.
            val liftIdx = if (masked == MotionEvent.ACTION_UP || masked == MotionEvent.ACTION_POINTER_UP)
                event.actionIndex else -1
            val list = ArrayList<Contact>(event.pointerCount)
            for (i in 0 until event.pointerCount) {
                if (i == liftIdx) continue
                val xn = ((event.getX(i) / w).coerceIn(0f, 1f) * 65535f).toInt()
                val yn = ((event.getY(i) / h).coerceIn(0f, 1f) * 65535f).toInt()
                // Pressure 0..1023 (capacitive screens report an approximation).
                val pn = (event.getPressure(i).coerceIn(0f, 1f) * 1023f).toInt()
                list.add(Contact(event.getPointerId(i), xn, yn, pn))
            }
            list
        }
        l.onTouch(contacts)
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
