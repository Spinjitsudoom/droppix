package com.droppix.app.audio

import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.util.Log
import java.util.concurrent.LinkedBlockingQueue

// Plays raw s16le/48k/stereo PCM via AudioTrack on a dedicated thread. The net
// thread only submit()s; playback (a blocking write) never stalls the net loop.
class AudioPlayer {
    companion object {
        private const val RATE = 48000
        private const val TAG = "droppix"

        // The host reads PCM in 4 KB chunks: 1024 stereo s16 frames = ~21.3 ms each.
        const val CHUNK_MS = 21.3

        /** ~256 ms of audio. Past this we are audibly behind, not absorbing jitter. */
        const val MAX_CHUNKS = 12

        /** Overflow drops back to here (~85 ms), leaving headroom instead of staying full. */
        const val LOW_WATER_CHUNKS = 4
    }

    // Every queued chunk is audio that has not been heard yet, so queue depth IS latency.
    //
    // This used to hold 64 chunks (~1.4s) and drop the oldest one when full. That bounded
    // memory but not lag: dropping a single chunk per overflow leaves the queue pinned at the
    // ceiling, so once a stall filled it the stream stayed ~1.4s behind for good. Playback
    // only advances in real time, so a backlog is never caught up — it has to be thrown away.
    //
    // Hold a much shorter budget, and on overflow drop down to a LOW-WATER mark so one glitch
    // costs a single audible gap instead of permanent lag.
    private val queue = LinkedBlockingQueue<ByteArray>(MAX_CHUNKS)
    @Volatile private var running = false
    private var thread: Thread? = null
    private var track: AudioTrack? = null

    fun start() {
        if (running) return
        val min = AudioTrack.getMinBufferSize(RATE,
            AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT)
        val bufSize = if (min > 0) min * 2 else 8192
        track = try {
            AudioTrack(AudioManager.STREAM_MUSIC, RATE, AudioFormat.CHANNEL_OUT_STEREO,
                AudioFormat.ENCODING_PCM_16BIT, bufSize, AudioTrack.MODE_STREAM)
        } catch (e: Exception) { Log.w(TAG, "AudioTrack init failed: ${e.message}"); null }
        val t = track ?: return
        t.play()
        running = true
        thread = Thread({ loop(t) }, "droppix-audio").apply { isDaemon = true; start() }
    }

    fun submit(pcm: ByteArray) {
        if (!running) return
        if (queue.offer(pcm)) return
        // Full: we are behind. Discard the oldest (stale) audio down to the low-water mark
        // rather than making room for exactly one chunk, which would leave us at the ceiling
        // and permanently late.
        while (queue.size > LOW_WATER_CHUNKS) queue.poll()
        queue.offer(pcm)
    }

    /**
     * Drop anything buffered but not yet played.
     *
     * Used when audio is muted mid-stream: without this the queued chunks would be played
     * out on unmute, replaying sound from before the mute.
     */
    fun flush() {
        queue.clear()
    }

    private fun loop(t: AudioTrack) {
        while (running) {
            val pcm = try { queue.take() } catch (e: InterruptedException) { break }
            try { t.write(pcm, 0, pcm.size) } catch (e: Exception) { break }
        }
    }

    fun release() {
        running = false
        thread?.interrupt(); thread?.join(500); thread = null
        try { track?.stop(); track?.release() } catch (_: Exception) {}
        track = null; queue.clear()
    }
}
