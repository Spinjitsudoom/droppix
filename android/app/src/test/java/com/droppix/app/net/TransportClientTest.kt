package com.droppix.app.net

import com.droppix.app.protocol.MsgType
import com.droppix.app.protocol.Protocol
import org.junit.Assert.*
import org.junit.Test
import java.io.DataInputStream
import java.net.ServerSocket
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread

class TransportClientTest {
    @Test fun handshakeReceivesConfigAndVideo() {
        val server = ServerSocket(0)
        val port = server.localPort

        // Fake host: accept, read HELLO, send CONFIG + one VIDEO.
        val serverThread = thread {
            server.use {
                val sock = it.accept()
                val input = DataInputStream(sock.getInputStream())
                // read HELLO frame: u32 len, then len bytes
                val len = input.readInt()
                val frame = ByteArray(len); input.readFully(frame)
                assertEquals(MsgType.HELLO.code, frame[0].toInt())  // type byte
                val out = sock.getOutputStream()
                out.write(Protocol.encodeMessage(MsgType.CONFIG,
                    beU32(1920) + beU32(1080) + beU32(30) + beU32(0)))  // empty extradata
                out.write(Protocol.encodeMessage(MsgType.VIDEO,
                    beU64(1000L) + byteArrayOf(1) + byteArrayOf(0,0,0,1,0x65)))
                out.flush()
                Thread.sleep(200)
                sock.close()
            }
        }

        var gotConfig: Protocol.Config? = null
        var gotVideo: Protocol.Video? = null
        val latch = CountDownLatch(2)
        val listener = object : StreamListener {
            override fun onConfig(config: Protocol.Config) { gotConfig = config; latch.countDown() }
            override fun onVideo(video: Protocol.Video) { gotVideo = video; latch.countDown() }
        }

        val client = TransportClient()
        val clientThread = thread {
            client.run("127.0.0.1", port, 1920, 1080, 320, listener) { true }
        }

        assertTrue("did not receive config+video", latch.await(3, TimeUnit.SECONDS))
        assertEquals(1920, gotConfig!!.width)
        assertEquals(1000L, gotVideo!!.ptsUs)
        assertTrue(gotVideo!!.keyframe)

        serverThread.join(1000); clientThread.join(1000)
    }

    private fun beU32(x: Int) = byteArrayOf(
        (x ushr 24).toByte(), (x ushr 16).toByte(), (x ushr 8).toByte(), x.toByte())
    private fun beU64(x: Long) = ByteArray(8) { i -> (x ushr (56 - i * 8)).toByte() }
}
