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

class StreamActivity : Activity(), DisplaySurfaceView.SurfaceListener {
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
    @Volatile private var surface: Surface? = null
    private var netThread: Thread? = null
    @Volatile private var decoder: VideoDecoder? = null
    @Volatile private var client: TransportClient? = null
    @Volatile private var audioPlayer: AudioPlayer? = null
    private lateinit var surfaceView: DisplaySurfaceView

    // Auto-orientation: the Activity follows the sensor (manifest fullSensor) so Android
    // rotates the display naturally. We detect the physical orientation and report it; on
    // a portrait<->landscape change the host restreams at swapped dims and we reconnect,
    // so the SurfaceView then matches the new (portrait/landscape) CONFIG size.
    private val orientationMapper = OrientationMapper()
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
        surfaceView = findViewById(R.id.surface)
        overlay = findViewById(R.id.overlay)
        overlay.visibility = View.GONE   // shown only if the host asks (Settings → performance overlay)
        applyImmersive()
        orientationListener = object : OrientationEventListener(this) {
            override fun onOrientationChanged(angleDeg: Int) {
                val code = orientationMapper.update(angleDeg, SystemClock.elapsedRealtime()) ?: return
                Log.i(TAG, "orientation -> $code")
                client?.sendOrientation(code)
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

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) applyImmersive()
    }

    override fun onResume() {
        super.onResume()
        surfaceView.setSurfaceListener(this)  // fires onSurfaceReady if already valid
        surfaceView.setTouchListener(object : DisplaySurfaceView.TouchListener {
            override fun onTouch(contacts: List<com.droppix.app.protocol.Contact>) {
                client?.sendTouch(contacts)
            }
        })
        uiHandler.post(overlayTick)
        orientationListener?.takeIf { it.canDetectOrientation() }?.enable()
    }

    override fun onPause() {
        super.onPause()
        uiHandler.removeCallbacks(overlayTick)
        orientationListener?.disable()
        surfaceView.setTouchListener(null)
        surfaceView.setSurfaceListener(null)
        stopStreaming()
    }

    // --- DisplaySurfaceView.SurfaceListener (UI thread) ---
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
                    runOnUiThread { surfaceView.holder.setFixedSize(config.width, config.height) }
                    decoder?.release()
                    decoder = try {
                        VideoDecoder(s, config.width, config.height, stats)
                    } catch (e: Exception) {
                        Log.w(TAG, "decoder create failed: ${e.message}"); null
                    }
                }
                override fun onVideo(video: Protocol.Video) {
                    sawVideo = true
                    decoder?.submit(video.nal, video.ptsUs)
                }
                override fun onAudio(pcm: ByteArray) { player.submit(pcm) }
                override fun onOverlay(show: Boolean) {
                    runOnUiThread { overlay.visibility = if (show) View.VISIBLE else View.GONE }
                }
            }
            val acc = aoaAccessory
            if (acc != null) {
                // AOA (USB cable): open the accessory and stream the protocol over its FD
                // streams — no TLS/PIN (the cable is the trust boundary). Retry the open if it
                // errors before any video arrives: the host's interface-claim can EIO the first
                // read (M0 finding). One connection; finishes when the cable is unplugged.
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
                            FileOutputStream(pfd.fileDescriptor), 1920, 1080,
                            resources.displayMetrics.densityDpi, listener, { running }, stats,
                            name = DeviceIdentity.displayName(this@StreamActivity),
                            id = DeviceIdentity.stableId(this@StreamActivity))
                        Log.i(TAG, "aoa: session ended")
                    } catch (e: Exception) {
                        Log.w(TAG, "aoa: attempt $attempt ended: ${e.message}")
                    } finally {
                        decoder?.release(); decoder = null
                        try { pfd.close() } catch (_: Exception) {}
                    }
                    if (sawVideo) break     // real data flowed then ended -> done
                    Thread.sleep(200)       // errored before any video -> retry the open
                }
                running = false
                runOnUiThread { finish() }
            } else {
                // The host re-accepts clients in a loop, so keep dialing until paused.
                while (running) {
                    try {
                        Log.i(TAG, "connecting to $host:$port")
                        c.run(host, port, 1920, 1080,
                            resources.displayMetrics.densityDpi, listener, { running }, stats,
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
