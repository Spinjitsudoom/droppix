package com.droppix.app.ui

import android.app.Activity
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import android.view.OrientationEventListener
import android.view.Surface
import android.view.WindowManager
import android.widget.TextView
import com.droppix.app.R
import com.droppix.app.decode.VideoDecoder
import com.droppix.app.net.DeviceIdentity
import com.droppix.app.net.StreamListener
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

    @Volatile private var running = false
    @Volatile private var surface: Surface? = null
    private var netThread: Thread? = null
    @Volatile private var decoder: VideoDecoder? = null
    @Volatile private var client: TransportClient? = null
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
        orientationListener = object : OrientationEventListener(this) {
            override fun onOrientationChanged(angleDeg: Int) {
                val code = orientationMapper.update(angleDeg, SystemClock.elapsedRealtime()) ?: return
                Log.i(TAG, "orientation -> $code")
                client?.sendOrientation(code)
            }
        }
    }

    override fun onResume() {
        super.onResume()
        surfaceView.setSurfaceListener(this)  // fires onSurfaceReady if already valid
        surfaceView.setTouchListener(object : DisplaySurfaceView.TouchListener {
            override fun onTouch(action: Int, xNorm: Int, yNorm: Int) {
                client?.sendInput(action, xNorm, yNorm)
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
            client = c
            val listener = object : StreamListener {
                override fun onConfig(config: Protocol.Config) {
                    Log.i(TAG, "CONFIG ${config.width}x${config.height}@${config.fps}")
                    saveLastEndpoint()  // handshake succeeded: remember this endpoint for "reconnect to last"
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
                    decoder?.submit(video.nal, video.ptsUs)
                }
            }
            // The host re-accepts clients in a loop, so keep dialing until paused.
            while (running) {
                try {
                    Log.i(TAG, "connecting to $host:$port")
                    c.run(host, port, 1920, 1080,
                        resources.displayMetrics.densityDpi, listener, { running }, stats,
                        name = DeviceIdentity.displayName(this@StreamActivity),
                        id = DeviceIdentity.stableId(this@StreamActivity))
                    Log.i(TAG, "stream session ended")
                } catch (e: Exception) {
                    Log.w(TAG, "connect/stream failed: ${e.message}")
                }
                decoder?.release(); decoder = null
                if (running) Thread.sleep(1000)  // back off before retrying
            }
            client = null
        }
    }

    private fun saveLastEndpoint() {
        getSharedPreferences("droppix", MODE_PRIVATE).edit()
            .putString("last_host", host).putInt("last_port", port).apply()
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
