package com.droppix.app.audio

import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.util.Log
import java.util.concurrent.LinkedBlockingQueue

// Plays raw s16le/48k/stereo PCM via AudioTrack on a dedicated thread. The net
// thread only submit()s; playback (a blocking write) never stalls the net loop.
class AudioPlayer {
    companion object { private const val RATE = 48000; private const val TAG = "droppix" }

    private val queue = LinkedBlockingQueue<ByteArray>(64)   // bounded -> latency stays low
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
        if (!queue.offer(pcm)) { queue.poll(); queue.offer(pcm) }   // drop oldest on overflow
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
