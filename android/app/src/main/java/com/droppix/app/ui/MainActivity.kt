package com.droppix.app.ui

import android.app.Activity
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.Surface
import android.view.WindowManager
import android.widget.TextView
import com.droppix.app.R
import com.droppix.app.decode.VideoDecoder
import com.droppix.app.net.StreamListener
import com.droppix.app.net.TransportClient
import com.droppix.app.protocol.Protocol
import com.droppix.app.stats.StatsSink
import kotlin.concurrent.thread

class MainActivity : Activity(), DisplaySurfaceView.SurfaceListener {
    private companion object {
        const val TAG = "droppix"; const val HOST = "127.0.0.1"; const val PORT = 27000
    }

    @Volatile private var running = false
    @Volatile private var surface: Surface? = null
    private var netThread: Thread? = null
    @Volatile private var decoder: VideoDecoder? = null
    @Volatile private var client: TransportClient? = null
    private lateinit var surfaceView: DisplaySurfaceView

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
        setContentView(R.layout.activity_main)
        surfaceView = findViewById(R.id.surface)
        overlay = findViewById(R.id.overlay)
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
    }

    override fun onPause() {
        super.onPause()
        uiHandler.removeCallbacks(overlayTick)
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
                    Log.i(TAG, "connecting to $HOST:$PORT")
                    c.run(HOST, PORT, 1920, 1080,
                        resources.displayMetrics.densityDpi, listener, { running }, stats)
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

    private fun stopStreaming() {
        running = false
        netThread?.join(1500)
        if (netThread?.isAlive == true) Log.w(TAG, "net thread did not exit within 1.5s")
        netThread = null
        decoder?.release()
        decoder = null
    }
}
