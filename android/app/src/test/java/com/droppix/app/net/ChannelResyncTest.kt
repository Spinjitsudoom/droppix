package com.droppix.app.net

import com.droppix.app.protocol.MsgType
import com.droppix.app.protocol.Protocol
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Over USB the app can open the accessory in the middle of one of the PC's messages (it closed
 * the old reader mid-frame). The parser then reads picture bytes as a length and may wait for a
 * message that never ends, while the link stays busy, so nothing else would ever notice.
 */
class ChannelResyncTest {
    /** Endless bytes, a few at a time, like video arriving on a misaligned stream. */
    private class Trickle(private val pattern: ByteArray) : InputStream() {
        private var i = 0
        override fun read(): Int = pattern[i++ % pattern.size].toInt() and 0xFF
        override fun read(b: ByteArray, off: Int, len: Int): Int {
            Thread.sleep(5)
            val n = minOf(len, 64)
            for (k in 0 until n) b[off + k] = pattern[(i + k) % pattern.size]
            i += n
            return n
        }
    }

    private val quiet = object : StreamListener {
        override fun onConfig(config: Protocol.Config) {}
        override fun onVideo(video: Protocol.Video) {}
    }

    @Test fun noConfigInTimeEndsTheSessionSoItCanReopen() {
        // 00 00 10 00 = a plausible 4 KiB length, then bytes that never line up again.
        val misaligned = Trickle(byteArrayOf(0, 0, 0x10, 0, 3, 0x41, 0x9a.toByte(), 0x22))
        val started = System.currentTimeMillis()
        var failure: Exception? = null
        try {
            TransportClient().runOverChannel(misaligned, ByteArrayOutputStream(), 768, 480, 320,
                60, 0, 0, 8000, 0, 0, quiet, { System.currentTimeMillis() - started < 10_000 },
                configTimeoutMs = 300)
        } catch (e: Exception) { failure = e }
        val took = System.currentTimeMillis() - started
        assertTrue("should give up on its own, took $took ms", failure != null && took < 3_000)
    }

    @Test fun aConfigInTimeKeepsTheSession() {
        val config = Protocol.encodeMessage(MsgType.CONFIG,
            byteArrayOf(0, 0, 3, 0, 0, 0, 1, 0xe0.toByte(), 0, 0, 0, 60, 0, 0, 0, 0))
        val ping = Protocol.encodeMessage(MsgType.PING, ByteArray(8))
        val host = Trickle(config + ping)   // CONFIG, then the session stays alive
        val running = AtomicBoolean(true)
        val started = System.currentTimeMillis()
        val t = Thread {
            TransportClient().runOverChannel(host, ByteArrayOutputStream(), 768, 480, 320,
                60, 0, 0, 8000, 0, 0, quiet, { running.get() }, configTimeoutMs = 300)
        }
        t.start()
        Thread.sleep(1_000)
        assertTrue("a session with a CONFIG must not be cut", t.isAlive)
        running.set(false); t.join(2_000)
        assertEquals(false, t.isAlive)
        assertTrue(System.currentTimeMillis() - started >= 1_000)
    }
}
