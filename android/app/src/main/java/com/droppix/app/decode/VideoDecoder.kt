package com.droppix.app.decode

import android.media.MediaCodec
import android.media.MediaFormat
import android.os.Build
import android.os.SystemClock
import android.util.Log
import android.view.Surface
import com.droppix.app.stats.StatsSink

// Hardware H.264 decode straight onto a Surface. SPS/PPS arrive in-band, so we
// configure with only width/height and let the codec sync on the first IDR.
class VideoDecoder(surface: Surface, width: Int, height: Int,
                   private val stats: StatsSink? = null) {
    private companion object { const val TAG = "droppix" }

    private val codec = MediaCodec.createDecoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
    private val info = MediaCodec.BufferInfo()
    @Volatile private var released = false
    private val submitNs = HashMap<Long, Long>()  // ptsUs -> submit SystemClock ns

    // Set by StreamActivity to ask the host for an IDR. Dropping a NAL breaks the decoder's
    // reference chain, so without this the picture stays corrupt until the host's next
    // scheduled keyframe (gop = fps*2, i.e. up to ~2s).
    var onNeedKeyframe: (() -> Unit)? = null
    private var lastRequestMs = 0L

    // Rate-limited: a burst of drops must not turn the stream all-intra.
    private fun requestKeyframe() {
        val now = SystemClock.elapsedRealtime()
        if (now - lastRequestMs < 500) return
        lastRequestMs = now
        onNeedKeyframe?.invoke()
    }

    init {
        val fmt = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height)
        if (Build.VERSION.SDK_INT >= 30) {
            fmt.setInteger(MediaFormat.KEY_LOW_LATENCY, 1)
        }
        // Generously size input buffers so a large IDR (with in-band SPS/PPS)
        // can't overflow the default allocation on conservative OEM decoders.
        fmt.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, maxOf(width * height, 1024 * 1024))
        codec.configure(fmt, surface, null, 0)
        codec.start()
    }

    @Synchronized
    fun submit(nal: ByteArray, ptsUs: Long) {
        if (released) return
        try {
            val inIndex = codec.dequeueInputBuffer(100_000)  // wait up to 100ms
            if (inIndex >= 0) {
                val buf = codec.getInputBuffer(inIndex)!!
                buf.clear()
                if (nal.size > buf.capacity()) {
                    // Don't crash on an oversized NAL; queue an empty buffer to keep
                    // the index accounted for and log so a frozen stream is diagnosable.
                    Log.w(TAG, "NAL ${nal.size}B exceeds input buffer ${buf.capacity()}B; dropping")
                    codec.queueInputBuffer(inIndex, 0, 0, ptsUs, 0)
                    requestKeyframe()
                } else {
                    buf.put(nal)
                    if (stats != null) submitNs[ptsUs] = SystemClock.elapsedRealtimeNanos()
                    codec.queueInputBuffer(inIndex, 0, nal.size, ptsUs, 0)
                }
            } else {
                Log.w(TAG, "no input buffer available; dropping ${nal.size}B NAL")
                requestKeyframe()
            }
            var outIndex = codec.dequeueOutputBuffer(info, 0)
            while (outIndex >= 0) {
                if (stats != null) {
                    val t0 = submitNs.remove(info.presentationTimeUs)
                    if (t0 != null) {
                        stats.decodeLagMs = (SystemClock.elapsedRealtimeNanos() - t0) / 1_000_000.0
                    }
                    if (submitNs.size > 240) submitNs.clear()  // safety bound
                }
                codec.releaseOutputBuffer(outIndex, true)  // render to the surface
                outIndex = codec.dequeueOutputBuffer(info, 0)
            }
        } catch (e: IllegalStateException) {
            Log.w(TAG, "decoder submit failed: ${e.message}")
            requestKeyframe()
        }
    }

    @Synchronized
    fun release() {
        released = true
        try { codec.stop() } catch (_: Exception) {}
        codec.release()
        submitNs.clear()
    }
}
