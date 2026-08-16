package com.droppix.app.ui

import android.app.Activity
import android.app.AlertDialog
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import android.view.OrientationEventListener
import android.view.Surface
import android.view.View
import android.view.WindowManager
import android.view.inputmethod.InputMethodManager
import android.widget.Button
import android.widget.TextView
import android.content.Context
import android.hardware.usb.UsbAccessory
import android.hardware.usb.UsbManager
import java.io.FileInputStream
import java.io.FileOutputStream
import com.droppix.app.R
import com.droppix.app.audio.AudioPlayer
import com.droppix.app.decode.VideoDecoder
import com.droppix.app.net.CertChangedException
import com.droppix.app.net.DeviceIdentity
import com.droppix.app.net.StreamListener
import com.droppix.app.net.TlsTrust
import com.droppix.app.net.TransportClient
import com.droppix.app.protocol.Protocol
import com.droppix.app.stats.StatsSink
import kotlin.concurrent.thread

class StreamActivity : Activity(), GlDisplayView.SurfaceListener {
    private companion object {
        const val TAG = "droppix"
    }

    private val host by lazy { intent.getStringExtra("host") ?: "127.0.0.1" }
    private val port by lazy { intent.getIntExtra("port", 27000) }
    // Set only when the system launched us from a USB_ACCESSORY_ATTACHED intent => stream over AOA.
    private val aoaAccessory: UsbAccessory? by lazy {
        intent.getParcelableExtra(UsbManager.EXTRA_ACCESSORY)
    }

    @Volatile private var running = false
    @Volatile private var rotationLocked = false
    @Volatile private var localOverlayWanted = false

    // Live quick-toggles from the floating menu. Both are client-side, so they take effect
    // on the running stream with no renegotiation: audio gates PLAYBACK of received PCM,
    // touch gates FORWARDING of input. Muting locally rather than renegotiating keeps the
    // toggle instant; the cost is that the host keeps sending audio we discard.
    @Volatile private var audioEnabled = true
    @Volatile private var touchEnabled = true

    /**
     * In-stream settings overlay. Display params apply to the running stream immediately;
     * negotiated params (resolution/fps/quality/audio) restart the session in place, without
     * ever leaving this screen.
     */
    private val settingsPanel: StreamSettingsPanel by lazy {
        StreamSettingsPanel(
            activity = this,
            store = com.droppix.app.settings.SettingsStore(this),
            applyLive = { s -> applyDisplaySettings(s) },
            reconnect = { restartSession() },
        )
    }

    /**
     * Floating action menu: a single button that expands to the actions worth reaching
     * mid-stream. Every one of them applies to the LIVE stream.
     */
    private fun wireFloatingMenu() {
        val actions = findViewById<View>(R.id.fab_actions)
        val fab = findViewById<Button>(R.id.fab_main)
        val audioBtn = findViewById<Button>(R.id.act_audio)
        val touchBtn = findViewById<Button>(R.id.act_touch)

        fun refresh() {
            audioBtn.text = if (audioEnabled) "Audio: on" else "Audio: off"
            touchBtn.text = if (touchEnabled) "Touch: on" else "Touch: off"
        }
        fun collapse() {
            actions.visibility = View.GONE
            fab.text = "\u2630"
        }
        refresh()

        fab.setOnClickListener {
            if (actions.visibility == View.VISIBLE) collapse()
            else {
                refresh()
                actions.visibility = View.VISIBLE
                fab.text = "\u00D7"
            }
        }
        findViewById<Button>(R.id.act_settings).setOnClickListener {
            collapse()
            settingsPanel.toggle()
        }
        findViewById<Button>(R.id.act_keyboard).setOnClickListener {
            collapse()
            toggleSoftKeyboard()
        }
        audioBtn.setOnClickListener {
            // Instant: stop feeding the player. No renegotiation, no stream interruption.
            audioEnabled = !audioEnabled
            if (!audioEnabled) audioPlayer?.flush()
            refresh()
        }
        touchBtn.setOnClickListener {
            // Instant: stop forwarding input, so the screen can be handled without driving
            // the desktop.
            touchEnabled = !touchEnabled
            refresh()
        }
    }

    /** Push display-only settings onto the live view. No protocol involvement. */
    private fun applyDisplaySettings(s: com.droppix.app.settings.AppSettings) {
        surfaceView.flipHorizontal = s.flipHorizontal
        surfaceView.brightness = s.brightness
        surfaceView.contrast = s.contrast
        localOverlayWanted = s.showOverlay
        overlay.visibility = if (s.showOverlay) View.VISIBLE else View.GONE
        rotationLocked = s.rotationLocked
        requestedOrientation = if (s.rotationLocked)
            android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LOCKED
        else
            android.content.pm.ActivityInfo.SCREEN_ORIENTATION_FULL_SENSOR
    }

    /**
     * Re-negotiate the session in place: resolution/fps/quality/audio travel in HELLO, so the
     * host can only honour a change on a new session. startStreaming() re-reads settings, so
     * stopping and starting is all that is required — and the Activity is never left, unlike
     * the old trip through SettingsActivity.
     */
    private fun restartSession() {
        stopStreaming()
        if (surface != null) startStreaming()
    }
    @Volatile private var surface: Surface? = null
    private var netThread: Thread? = null
    @Volatile private var decoder: VideoDecoder? = null
    @Volatile private var client: TransportClient? = null
    @Volatile private var audioPlayer: AudioPlayer? = null
    private lateinit var surfaceView: GlDisplayView
    private var imeShown = false

    // Auto-orientation: the Activity follows the sensor (manifest fullSensor) so Android
    // rotates the display naturally. We detect the physical orientation and report it; on
    // a portrait<->landscape change the host restreams at swapped dims and we reconnect,
    // so the SurfaceView then matches the new (portrait/landscape) CONFIG size.
    // Built lazily so the display is available: a phone is portrait-natural, a tablet
    // landscape-natural, and the sensor angle means opposite things on the two.
    private val orientationMapper by lazy { OrientationMapper(naturalIsPortrait = naturalIsPortrait()) }

    /**
     * Is this device's NATURAL orientation (rotation 0) portrait?
     *
     * Compare the current shape against the current rotation: at ROTATION_0/180 the device
     * is in its natural shape, at 90/270 it is turned a quarter from it.
     */
    private fun naturalIsPortrait(): Boolean {
        val m = android.util.DisplayMetrics()
        @Suppress("DEPRECATION") val display = windowManager.defaultDisplay
        @Suppress("DEPRECATION") display.getRealMetrics(m)
        val tallNow = m.heightPixels >= m.widthPixels
        @Suppress("DEPRECATION") val rot = display.rotation
        val quarterTurned = rot == android.view.Surface.ROTATION_90 || rot == android.view.Surface.ROTATION_270
        return if (quarterTurned) !tallNow else tallNow
    }
    private var orientationListener: OrientationEventListener? = null

    private val stats = StatsSink()
    private val uiHandler = Handler(Looper.getMainLooper())
    private lateinit var overlay: TextView
    private val overlayTick = object : Runnable {
        override fun run() {
            overlay.text = String.format(
                "RTT %.0f ms  |  fps %.0f  |  decode %.0f ms",
                stats.rttMs, stats.fps, stats.decodeLagMs)
            uiHandler.postDelayed(this, 1000)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        setContentView(R.layout.activity_stream)
        // Don't let the IME resize/pan the window when it appears: the surface is a fixed-size
        // video sink, not scrollable content.
        window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING)
        surfaceView = findViewById(R.id.surface)
        // In-stream entry point to Settings. Must be a real overlay View (topmost FrameLayout
        // child) rather than a long-press on the surface: GlDisplayView.onTouchEvent
        // consumes MotionEvents without calling super, so View long-press detection never runs.
        wireFloatingMenu()
        overlay = findViewById(R.id.overlay)
        overlay.visibility = View.GONE   // shown only if the host asks (Settings → performance overlay)
        applyImmersive()
        orientationListener = object : OrientationEventListener(this) {
            override fun onOrientationChanged(angleDeg: Int) {
                val code = orientationMapper.update(angleDeg, SystemClock.elapsedRealtime()) ?: return
                Log.i(TAG, "orientation -> $code")
                if (!rotationLocked) client?.sendOrientation(code)
            }
        }
    }

    // Hide the status + navigation bars for a true full-screen monitor; a swipe from
    // the edge peeks them back, then they auto-hide again (immersive sticky).
    private fun applyImmersive() {
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility =
            (View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_FULLSCREEN
                or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY)
    }

    // Toggles the Android soft keyboard on/off, targeting surfaceView so IME text lands in
    // Task 2's InputConnection -> keyListener -> client.sendKey path. Dismissing via Back may
    // desync imeShown by one tap (acceptable; the next tap re-syncs).
    private fun toggleSoftKeyboard() {
        val imm = getSystemService(INPUT_METHOD_SERVICE) as InputMethodManager
        if (imeShown) {
            imm.hideSoftInputFromWindow(surfaceView.windowToken, 0)
        } else {
            surfaceView.requestFocus()
            imm.showSoftInput(surfaceView, 0)
        }
        imeShown = !imeShown
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) applyImmersive()
    }

    @Suppress("DEPRECATION")
    override fun onBackPressed() {
        // Back should close the overlay, not drop the session the user is looking at.
        if (settingsPanel.isOpen) settingsPanel.hide() else super.onBackPressed()
    }

    override fun onResume() {
        super.onResume()
        surfaceView.setSurfaceListener(this)  // fires onSurfaceReady if already valid
        surfaceView.setTouchListener(object : GlDisplayView.TouchListener {
            override fun onTouch(contacts: List<com.droppix.app.protocol.Contact>) {
                if (touchEnabled) client?.sendTouch(contacts)
            }
        })
        surfaceView.setMouseListener(object : GlDisplayView.MouseListener {
            override fun onScroll(dx: Int, dy: Int, x: Int, y: Int) {
                if (touchEnabled) client?.sendScroll(dx, dy, x, y)
            }
            override fun onMouseButton(button: Int, action: Int, x: Int, y: Int) {
                if (touchEnabled) client?.sendMouseButton(button, action, x, y)
            }
        })
        surfaceView.setKeyListener(object : GlDisplayView.KeyListener {
            override fun onKey(keycode: Int, action: Int) { client?.sendKey(keycode, action) }
        })
        surfaceView.setPenListener(object : GlDisplayView.PenListener {
            override fun onPen(x: Int, y: Int, pressure: Int, flags: Int) {
                if (touchEnabled) client?.sendPen(x, y, pressure, flags)
            }
        })
        surfaceView.requestFocus()
        // Live-apply: brightness/contrast are display-only shader params, so a settings change
        // that touches only these is pushed straight onto the surface here (every resume) rather
        // than routed through the reconnect path in startStreaming() — cheap field sets, no
        // restream needed.
        val s = com.droppix.app.settings.SettingsStore(this).load()
        surfaceView.brightness = s.brightness
        surfaceView.contrast = s.contrast
        uiHandler.post(overlayTick)
        orientationListener?.takeIf { it.canDetectOrientation() }?.enable()
    }

    override fun onPause() {
        super.onPause()
        uiHandler.removeCallbacks(overlayTick)
        orientationListener?.disable()
        surfaceView.setTouchListener(null)
        surfaceView.setMouseListener(null)
        surfaceView.setKeyListener(null)
        surfaceView.setPenListener(null)
        surfaceView.setSurfaceListener(null)
        stopStreaming()
    }

    // --- GlDisplayView.SurfaceListener (UI thread) ---
    override fun onSurfaceReady(surface: Surface) {
        this.surface = surface
        startStreaming()
    }

    override fun onSurfaceGone() {
        stopStreaming()
        surface = null
    }

    private fun startStreaming() {
        if (running) return
        running = true
        // Fresh load each time we start: returning from SettingsActivity resumes the Activity,
        // and the onResume -> setSurfaceListener -> onSurfaceReady -> startStreaming path re-reads
        // the just-saved settings here, so a settings change made mid-stream reconnects with the
        // new resolution/fps/audio. (onPause already stopped the previous session.)
        val settings = com.droppix.app.settings.SettingsStore(this).load()
        surfaceView.flipHorizontal = settings.flipHorizontal
        surfaceView.brightness = settings.brightness
        surfaceView.contrast = settings.contrast
        val real = android.util.DisplayMetrics()
        @Suppress("DEPRECATION") windowManager.defaultDisplay.getRealMetrics(real)
        val (sendW, sendH) = com.droppix.app.settings.Resolutions.resolve(settings, real.widthPixels, real.heightPixels)
        val sendFps = settings.fps
        val sendAudio = if (settings.audio) 1 else 0
        val sendBitrate = settings.bitrateKbps
        rotationLocked = settings.rotationLocked
        requestedOrientation = if (settings.rotationLocked)
            android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LOCKED
        else
            android.content.pm.ActivityInfo.SCREEN_ORIENTATION_FULL_SENSOR
        localOverlayWanted = settings.showOverlay
        overlay.visibility = if (settings.showOverlay) View.VISIBLE else View.GONE
        netThread = thread(name = "droppix-net") {
            val c = TransportClient()
            val tlsTrust = TlsTrust(this@StreamActivity)
            client = c
            var sawVideo = false
            val player = AudioPlayer().apply { start() }
            audioPlayer = player
            val listener = object : StreamListener {
                override fun onConfig(config: Protocol.Config) {
                    Log.i(TAG, "CONFIG ${config.width}x${config.height}@${config.fps}")
                    c.sendOrientation(orientationMapper.currentCode())  // sync host to current orientation
                    val s = surface ?: return
                    runOnUiThread {
                        // Deliberately NOT setFixedSize(config.width, config.height).
                        //
                        // That pinned the GL surface to the VIDEO size, so onSurfaceChanged
                        // reported the video's dimensions as the view size and AspectFit
                        // compared the video against ITSELF — always "perfect fit, no bars".
                        // SurfaceFlinger then scaled that buffer onto the real view rect,
                        // stretching non-uniformly whenever the video aspect differed from
                        // the screen's (e.g. a 2336x1080 stream on a 2340x1080 panel).
                        // Leaving the surface at the view's own size lets AspectFit do the
                        // letterboxing it was written for.
                        surfaceView.setVideoSize(config.width, config.height)
                    }
                    decoder?.release()
                    decoder = try {
                        VideoDecoder(s, config.width, config.height, stats).also { d ->
                            // A dropped NAL breaks the reference chain; ask the host for an
                            // IDR instead of showing corruption until its next scheduled one.
                            d.onNeedKeyframe = { client?.sendKeyframeRequest() }
                        }
                    } catch (e: Exception) {
                        Log.w(TAG, "decoder create failed: ${e.message}"); null
                    }
                }
                override fun onVideo(video: Protocol.Video) {
                    sawVideo = true
                    decoder?.submit(video.nal, video.ptsUs)
                }
                override fun onAudio(pcm: ByteArray) { if (audioEnabled) player.submit(pcm) }
                override fun onOverlay(show: Boolean) {
                    runOnUiThread { overlay.visibility = if (show || localOverlayWanted) View.VISIBLE else View.GONE }
                }
            }
            val acc = aoaAccessory
            if (acc != null) {
                // AOA (USB cable): open the accessory and stream the protocol over its FD
                // streams — no TLS/PIN (the cable is the trust boundary). Retry the open if it
                // errors before any video arrives: the host's interface-claim can EIO the first
                // read (M0 finding). Keep serving across session ends (host restart, USB hiccup,
                // orientation-driven restream) like the Wi-Fi path does — a successful session
                // resets the retry budget, so we only give up after 20 CONSECUTIVE failures
                // (~4s), which is what an actual unplug looks like (openAccessory returns null).
                val usb = getSystemService(Context.USB_SERVICE) as UsbManager
                // Let the host finish claiming the interface before we open the accessory — opening
                // mid-claim EIOs the first read (M0: opening late, after a manual tap, avoided it).
                Thread.sleep(1200)
                var attempt = 0
                while (running && attempt < 20) {
                    attempt++
                    sawVideo = false
                    val pfd = usb.openAccessory(acc)
                    if (pfd == null) { Log.w(TAG, "aoa: openAccessory null ($attempt)"); Thread.sleep(200); continue }
                    try {
                        Log.i(TAG, "aoa: streaming (attempt $attempt)")
                        c.runOverChannel(FileInputStream(pfd.fileDescriptor),
                            FileOutputStream(pfd.fileDescriptor), sendW, sendH,
                            resources.displayMetrics.densityDpi, sendFps, sendAudio, orientationMapper.currentCode(),
                            sendBitrate,
                            settings.wallCol, settings.wallRow,
                            listener, { running }, stats,
                            name = DeviceIdentity.displayName(this@StreamActivity),
                            id = DeviceIdentity.stableId(this@StreamActivity))
                        Log.i(TAG, "aoa: session ended")
                    } catch (e: Exception) {
                        Log.w(TAG, "aoa: attempt $attempt ended: ${e.message}")
                    } finally {
                        decoder?.release(); decoder = null
                        try { pfd.close() } catch (_: Exception) {}
                    }
                    if (sawVideo) attempt = 0  // real session ended -> reconnect with a fresh budget
                    Thread.sleep(200)
                }
                running = false
                runOnUiThread { finish() }
            } else {
                // The host re-accepts clients in a loop, so keep dialing until paused.
                while (running) {
                    try {
                        Log.i(TAG, "connecting to $host:$port")
                        c.run(host, port, sendW, sendH,
                            resources.displayMetrics.densityDpi, sendFps, sendAudio, orientationMapper.currentCode(),
                            sendBitrate,
                            settings.wallCol, settings.wallRow,
                            listener, { running }, stats,
                            name = DeviceIdentity.displayName(this@StreamActivity),
                            id = DeviceIdentity.stableId(this@StreamActivity),
                            tlsTrust = tlsTrust)
                        Log.i(TAG, "stream session ended")
                    } catch (e: CertChangedException) {
                        Log.w(TAG, "cert changed for $host: ${e.message}")
                        running = false
                        runOnUiThread { showCertChangedDialog(tlsTrust) }
                    } catch (e: IllegalStateException) {
                        Log.w(TAG, "not paired for $host: ${e.message}")
                        running = false
                        runOnUiThread { finish() }
                    } catch (e: Exception) {
                        Log.w(TAG, "connect/stream failed: ${e.message}")
                    }
                    decoder?.release(); decoder = null
                    if (running) Thread.sleep(1000)  // back off before retrying
                }
            }
            client = null
            c.close()
            player.release(); audioPlayer = null
        }
    }

    private fun showCertChangedDialog(tlsTrust: TlsTrust) {
        AlertDialog.Builder(this)
            .setTitle("PC identity changed")
            .setMessage("The PC's security identity changed since you paired. Re-pair?")
            .setPositiveButton("Re-pair") { _, _ ->
                tlsTrust.clear(host)
                finish()
            }
            .setNegativeButton("Cancel") { _, _ -> finish() }
            .setCancelable(false)
            .show()
    }

    private fun stopStreaming() {
        running = false
        netThread?.join(1500)
        if (netThread?.isAlive == true) Log.w(TAG, "net thread did not exit within 1.5s")
        netThread = null
        decoder?.release()
        decoder = null
    }
}
