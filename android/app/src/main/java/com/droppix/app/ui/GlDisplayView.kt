package com.droppix.app.ui

import android.content.Context
import android.graphics.SurfaceTexture
import android.opengl.GLES11Ext
import android.opengl.GLES20
import android.opengl.GLSurfaceView
import android.opengl.Matrix
import android.text.InputType
import android.util.AttributeSet
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.Surface
import android.view.SurfaceHolder
import android.view.View
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import com.droppix.app.protocol.Contact
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class GlDisplayView @JvmOverloads constructor(context: Context, attrs: AttributeSet? = null)
    : GLSurfaceView(context, attrs) {

    // ---- ported from the prior SurfaceView-based display view (verbatim): SurfaceListener,
    // ---- TouchListener, setSurfaceListener/setTouchListener, and the full onTouchEvent contacts logic ----

    interface SurfaceListener {
        fun onSurfaceReady(surface: Surface)
        fun onSurfaceGone()
    }

    // Multi-touch: the full set of active contacts (each normalized to 0..65535 of the view),
    // sent to the host every event. An empty list means all fingers lifted.
    interface TouchListener { fun onTouch(contacts: List<Contact>) }

    // Physical-mouse extras (scroll wheel, right/middle click); cursor movement and left-click
    // stay on the existing touch path above. x/y are normalized 0..65535 the same way as touch.
    interface MouseListener {
        fun onScroll(dx: Int, dy: Int, x: Int, y: Int)
        fun onMouseButton(button: Int, action: Int, x: Int, y: Int)
    }

    // Physical/Bluetooth hardware keyboard. keycode is an evdev code (KeyMap.toEvdev);
    // action follows the wire protocol's KEY body (0=up, 1=down, 2=repeat).
    interface KeyListener { fun onKey(keycode: Int, action: Int) }

    private var surfaceListener: SurfaceListener? = null
    private var touchListener: TouchListener? = null
    @Volatile private var mouseListener: MouseListener? = null
    @Volatile private var keyListener: KeyListener? = null
    private var lastMoveSentMs = 0L
    private val moveMinIntervalMs = 12L   // coalesce MOVEs to ~80 Hz max

    // Evdev codes currently held down, so a focus loss (backgrounding, a dialog, the soft
    // keyboard) can flush them as releases rather than leaving them stuck DOWN on the host.
    private val heldKeys = mutableSetOf<Int>()

    // Tracks the most recent SurfaceTexture-backed decode Surface. The prior SurfaceView-based
    // view tracked readiness via holder.surface (the SurfaceView's own on-screen surface, which doubled as
    // the decode target); here the decode target is a separate SurfaceTexture-backed Surface
    // created in GlRenderer.onSurfaceCreated, so it is tracked explicitly to preserve the same
    // "fires onSurfaceReady immediately if already valid" contract for a listener registered late.
    // Written on the GL thread (onSurfaceCreated), read on the UI thread (maybeDeliverSurface).
    @Volatile private var lastSurface: Surface? = null

    // UI-thread-only dedupe guard: the surface already delivered to the current listener. Both
    // delivery triggers (onSurfaceCreated's post, setSurfaceListener's post) route through
    // maybeDeliverSurface() on the UI thread; whichever runs first delivers and records the
    // surface here, and the second no-ops. Prevents onSurfaceReady firing twice for one surface
    // (which would spin up a second decode pipeline on a live surface — black screen / crash).
    private var deliveredSurface: Surface? = null

    // Deliver onSurfaceReady exactly once per (listener, surface) pair. MUST run on the UI thread.
    private fun maybeDeliverSurface() {
        val l = surfaceListener ?: return
        val s = lastSurface ?: return
        if (deliveredSurface === s) return
        deliveredSurface = s
        l.onSurfaceReady(s)
    }

    fun setTouchListener(l: TouchListener?) { touchListener = l }
    fun setMouseListener(l: MouseListener?) { mouseListener = l }
    fun setKeyListener(l: KeyListener?) { keyListener = l }

    // Same 0..65535 normalization the touch-contact loop below applies (view-local pixels ->
    // fraction of width/height, clamped, scaled). Reused by the mouse scroll/button branches so
    // both input paths land on identical coordinates.
    private fun normX(x: Float): Int {
        val w = width.coerceAtLeast(1)
        return ((x / w).coerceIn(0f, 1f) * 65535f).toInt()
    }
    private fun normY(y: Float): Int {
        val h = height.coerceAtLeast(1)
        return ((y / h).coerceIn(0f, 1f) * 65535f).toInt()
    }

    // Mouse wheel: SOURCE_MOUSE ACTION_SCROLL carries the wheel delta as axis values, not a
    // pointer move, so it arrives here rather than onTouchEvent. Cursor movement/left-click are
    // untouched — they still flow through the existing touch path.
    override fun onGenericMotionEvent(event: MotionEvent): Boolean {
        if (event.source and InputDevice.SOURCE_MOUSE != 0 && event.action == MotionEvent.ACTION_SCROLL) {
            val v = Math.round(event.getAxisValue(MotionEvent.AXIS_VSCROLL))
            val h = Math.round(event.getAxisValue(MotionEvent.AXIS_HSCROLL))
            if (v != 0 || h != 0) mouseListener?.onScroll(h, v, normX(event.x), normY(event.y))
            return true
        }
        return super.onGenericMotionEvent(event)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        // Physical-mouse right/middle click: reported as SOURCE_MOUSE BUTTON_PRESS/RELEASE
        // rather than a finger down/up, so it's branched off before the touch/left-click path
        // below (which handles ACTION_DOWN/MOVE/UP etc. for both fingers and the mouse's primary
        // button unchanged).
        if (event.source and InputDevice.SOURCE_MOUSE != 0 &&
            (event.actionMasked == MotionEvent.ACTION_BUTTON_PRESS || event.actionMasked == MotionEvent.ACTION_BUTTON_RELEASE)) {
            val down = event.actionMasked == MotionEvent.ACTION_BUTTON_PRESS
            val btn = when (event.actionButton) {
                MotionEvent.BUTTON_SECONDARY -> 1   // right
                MotionEvent.BUTTON_TERTIARY -> 2    // middle
                else -> 0
            }
            if (btn != 0) {
                mouseListener?.onMouseButton(btn, if (down) 1 else 0, normX(event.x), normY(event.y))
                return true
            }
        }
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

    // Physical/Bluetooth hardware keyboard. Unmapped keys (KeyMap.toEvdev == 0) fall through to
    // super so Android system keys (Back/Home/volume, etc.) keep working normally.
    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        val e = KeyMap.toEvdev(keyCode)
        if (e == 0) return super.onKeyDown(keyCode, event)
        heldKeys.add(e)
        keyListener?.onKey(e, if (event.repeatCount > 0) 2 else 1)
        return true
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        val e = KeyMap.toEvdev(keyCode)
        if (e == 0) return super.onKeyUp(keyCode, event)
        heldKeys.remove(e)
        keyListener?.onKey(e, 0)
        return true
    }

    // A modifier held while the window loses focus (backgrounding, a dialog, the soft
    // keyboard, etc.) would otherwise never see its up event, leaving it stuck DOWN on the
    // host. The listener is still set at this point (StreamActivity.onPause clears it
    // separately, after this fires), so the flush reaches the transport.
    override fun onWindowFocusChanged(hasWindowFocus: Boolean) {
        super.onWindowFocusChanged(hasWindowFocus)
        if (!hasWindowFocus) {
            val l = keyListener
            if (l != null) heldKeys.forEach { l.onKey(it, 0) }
            heldKeys.clear()
        }
    }

    override fun onCheckIsTextEditor(): Boolean = true

    override fun onCreateInputConnection(outAttrs: EditorInfo): InputConnection {
        // VISIBLE_PASSWORD + NO_SUGGESTIONS disables predictive composing, so the IME commits
        // each character immediately (commitText per char) instead of a composing region we'd
        // have to reconcile. NO_FULLSCREEN keeps the extract editor from covering the stream.
        outAttrs.inputType = InputType.TYPE_CLASS_TEXT or
            InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD or
            InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
        outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI or EditorInfo.IME_FLAG_NO_FULLSCREEN
        return KeyInputConnection(this, false)
    }

    // Soft-keyboard input arrives as committed text / deletions (not KeyEvents). Translate each
    // char to an evdev keycode (+ Shift) and push it through the same keyListener the physical
    // keyboard uses. Enter/Del that an IME sends as real KeyEvents flow through onKeyDown/onKeyUp
    // via BaseInputConnection.sendKeyEvent's default, so they need no handling here.
    private inner class KeyInputConnection(v: View, full: Boolean) : BaseInputConnection(v, full) {
        override fun commitText(text: CharSequence?, newCursorPosition: Int): Boolean {
            text?.forEach { typeChar(it) }
            return true
        }
        override fun deleteSurroundingText(beforeLength: Int, afterLength: Int): Boolean {
            repeat(beforeLength) { tapKey(14) }   // KEY_BACKSPACE
            repeat(afterLength)  { tapKey(111) }  // KEY_DELETE
            return true
        }
        override fun performEditorAction(actionCode: Int): Boolean {
            tapKey(28)   // KEY_ENTER — some IMEs deliver the action key instead of a KEYCODE_ENTER
            return true
        }
    }

    private fun tapKey(evdev: Int) {
        val l = keyListener ?: return
        l.onKey(evdev, 1); l.onKey(evdev, 0)
    }

    private fun typeChar(c: Char) {
        val (evdev, shift) = CharMap.toEvdev(c) ?: return
        val l = keyListener ?: return
        if (shift) l.onKey(42, 1)        // KEY_LEFTSHIFT down
        l.onKey(evdev, 1); l.onKey(evdev, 0)
        if (shift) l.onKey(42, 0)        // KEY_LEFTSHIFT up
    }

    // Register (or clear with null) the lifecycle listener. If the decode surface is already
    // valid (e.g. re-registering after a settings round-trip), onSurfaceReady still fires —
    // same contract as the prior SurfaceView-based view's setSurfaceListener, adapted to lastSurface (see above).
    // Delivery is posted (not synchronous) so it runs on the UI thread and shares the single
    // idempotent maybeDeliverSurface() path with onSurfaceCreated's post, deduping via
    // deliveredSurface so the surface is never delivered twice.
    fun setSurfaceListener(l: SurfaceListener?) {
        surfaceListener = l
        if (l != null) post { maybeDeliverSurface() }
    }

    // GLSurfaceView already implements SurfaceHolder.Callback internally to drive its GL
    // thread; overriding surfaceDestroyed (chaining to super so EGL teardown still happens)
    // lets us signal onSurfaceGone at the same trigger point the prior SurfaceView-based view used —
    // the on-screen surface being torn down.
    override fun surfaceDestroyed(holder: SurfaceHolder) {
        super.surfaceDestroyed(holder)
        // Clear the dedupe guard so a later recreated surface — even one the JVM reuses as an
        // object-equal instance — is not wrongly suppressed by maybeDeliverSurface.
        deliveredSurface = null
        surfaceListener?.onSurfaceGone()
    }

    // ---- end ported members ----

    @Volatile var flipHorizontal: Boolean = false
    @Volatile var brightness: Int = 0
    @Volatile var contrast: Int = 100

    private val renderer = GlRenderer()

    init {
        isFocusableInTouchMode = true
        setEGLContextClientVersion(2)
        setRenderer(renderer)
        renderMode = RENDERMODE_WHEN_DIRTY
    }

    private inner class GlRenderer : Renderer {
        private var program = 0
        private var aPosition = 0
        private var aTexCoord = 0
        private var uTexMatrix = 0
        private var uTex = 0
        private var uBrightness = 0
        private var uContrast = 0
        private var texId = 0
        private var surfaceTexture: SurfaceTexture? = null
        private val stMatrix = FloatArray(16)
        private val texMatrix = FloatArray(16)
        private val mirror = FloatArray(16)

        // Fullscreen triangle-strip quad: clip-space positions + texcoords.
        private val quad: FloatBuffer = floatBuf(floatArrayOf(
            //   x,    y,     u, v
            -1f, -1f,   0f, 0f,
             1f, -1f,   1f, 0f,
            -1f,  1f,   0f, 1f,
             1f,  1f,   1f, 1f))

        override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
            program = buildProgram(VERT, FRAG)
            aPosition = GLES20.glGetAttribLocation(program, "aPosition")
            aTexCoord = GLES20.glGetAttribLocation(program, "aTexCoord")
            uTexMatrix = GLES20.glGetUniformLocation(program, "uTexMatrix")
            uTex = GLES20.glGetUniformLocation(program, "uTex")
            uBrightness = GLES20.glGetUniformLocation(program, "uBrightness")
            uContrast = GLES20.glGetUniformLocation(program, "uContrast")

            val ids = IntArray(1); GLES20.glGenTextures(1, ids, 0); texId = ids[0]
            GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, texId)
            GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
            GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
            GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
            GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)

            val st = SurfaceTexture(texId)
            st.setOnFrameAvailableListener { requestRender() }
            surfaceTexture = st
            val surface = Surface(st)
            lastSurface = surface
            // Hand the decode Surface to the activity on the UI thread, deduped via
            // maybeDeliverSurface so a concurrent setSurfaceListener post can't double-deliver.
            post { maybeDeliverSurface() }
        }

        override fun onSurfaceChanged(gl: GL10?, w: Int, h: Int) = GLES20.glViewport(0, 0, w, h)

        override fun onDrawFrame(gl: GL10?) {
            val st = surfaceTexture ?: return
            st.updateTexImage()
            st.getTransformMatrix(stMatrix)
            if (flipHorizontal) {
                // mirror about S=0.5, then apply the codec's stMatrix: texMatrix = stMatrix * mirror
                Matrix.setIdentityM(mirror, 0)
                Matrix.translateM(mirror, 0, 0.5f, 0f, 0f)
                Matrix.scaleM(mirror, 0, -1f, 1f, 1f)
                Matrix.translateM(mirror, 0, -0.5f, 0f, 0f)
                Matrix.multiplyMM(texMatrix, 0, stMatrix, 0, mirror, 0)
            } else {
                System.arraycopy(stMatrix, 0, texMatrix, 0, 16)
            }
            GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
            GLES20.glUseProgram(program)
            GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
            GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, texId)
            GLES20.glUniform1i(uTex, 0)
            GLES20.glUniformMatrix4fv(uTexMatrix, 1, false, texMatrix, 0)
            GLES20.glUniform1f(uBrightness, brightness / 200f)
            GLES20.glUniform1f(uContrast, contrast / 100f)
            quad.position(0)
            GLES20.glVertexAttribPointer(aPosition, 2, GLES20.GL_FLOAT, false, 16, quad)
            GLES20.glEnableVertexAttribArray(aPosition)
            quad.position(2)
            GLES20.glVertexAttribPointer(aTexCoord, 2, GLES20.GL_FLOAT, false, 16, quad)
            GLES20.glEnableVertexAttribArray(aTexCoord)
            GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        }
    }

    companion object {
        private const val VERT = """
            attribute vec4 aPosition;
            attribute vec4 aTexCoord;
            uniform mat4 uTexMatrix;
            varying vec2 vTexCoord;
            void main() {
                gl_Position = aPosition;
                vTexCoord = (uTexMatrix * aTexCoord).xy;
            }
        """
        private const val FRAG = """
            #extension GL_OES_EGL_image_external : require
            precision mediump float;
            varying vec2 vTexCoord;
            uniform samplerExternalOES uTex;
            uniform float uBrightness;
            uniform float uContrast;
            void main() {
                vec4 c = texture2D(uTex, vTexCoord);
                c.rgb = (c.rgb - 0.5) * uContrast + 0.5 + uBrightness;
                gl_FragColor = vec4(clamp(c.rgb, 0.0, 1.0), c.a);
            }
        """
        private fun floatBuf(a: FloatArray): FloatBuffer =
            ByteBuffer.allocateDirect(a.size * 4).order(ByteOrder.nativeOrder())
                .asFloatBuffer().apply { put(a); position(0) }
        private fun buildProgram(vs: String, fs: String): Int {
            fun sh(type: Int, src: String): Int {
                val s = GLES20.glCreateShader(type); GLES20.glShaderSource(s, src); GLES20.glCompileShader(s)
                val ok = IntArray(1); GLES20.glGetShaderiv(s, GLES20.GL_COMPILE_STATUS, ok, 0)
                check(ok[0] != 0) { "shader compile: " + GLES20.glGetShaderInfoLog(s) }
                return s
            }
            val p = GLES20.glCreateProgram()
            GLES20.glAttachShader(p, sh(GLES20.GL_VERTEX_SHADER, vs))
            GLES20.glAttachShader(p, sh(GLES20.GL_FRAGMENT_SHADER, fs))
            GLES20.glLinkProgram(p)
            val ok = IntArray(1); GLES20.glGetProgramiv(p, GLES20.GL_LINK_STATUS, ok, 0)
            check(ok[0] != 0) { "program link: " + GLES20.glGetProgramInfoLog(p) }
            return p
        }
    }
}
