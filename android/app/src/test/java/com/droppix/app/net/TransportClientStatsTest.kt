package com.droppix.app.net

import com.droppix.app.protocol.MsgType
import com.droppix.app.protocol.Protocol
import com.droppix.app.stats.StatsSink
import org.junit.Assert.*
import org.junit.Test
import java.io.DataInputStream
import java.security.KeyStore
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLServerSocket
import javax.net.ssl.SSLServerSocketFactory
import kotlin.concurrent.thread

private fun statsTestServerSocketFactory(): SSLServerSocketFactory {
    val ks = KeyStore.getInstance("PKCS12")
    TransportClientStatsTest::class.java.getResourceAsStream("/test_server.p12").use {
        ks.load(it, "droppix123".toCharArray())
    }
    val kmf = javax.net.ssl.KeyManagerFactory.getInstance(javax.net.ssl.KeyManagerFactory.getDefaultAlgorithm())
    kmf.init(ks, "droppix123".toCharArray())
    val context = SSLContext.getInstance("TLS")
    context.init(kmf.keyManagers, null, null)
    return context.serverSocketFactory
}

class TransportClientStatsTest {
    @Test fun rttAndFpsArePopulated() {
        val server = statsTestServerSocketFactory().createServerSocket(0) as SSLServerSocket
        val port = server.localPort
        val stop = AtomicBoolean(false)

        // Fake host: read HELLO, send CONFIG + a VIDEO, then echo any PING as PONG.
        val serverThread = thread {
            server.use {
                val sock = it.accept() as javax.net.ssl.SSLSocket
                sock.startHandshake()
                val input = DataInputStream(sock.getInputStream())
                val out = sock.getOutputStream()
                // read HELLO
                var len = input.readInt(); input.readFully(ByteArray(len))
                out.write(Protocol.encodeMessage(MsgType.CONFIG,
                    beU32(640) + beU32(480) + beU32(30) + beU32(0)))
                out.write(Protocol.encodeMessage(MsgType.VIDEO,
                    beU64(1L) + byteArrayOf(1) + byteArrayOf(0,0,0,1,0x65)))
                out.flush()
                // echo one PING -> PONG
                len = input.readInt()
                val frame = ByteArray(len); input.readFully(frame)
                if (frame[0].toInt() == MsgType.PING.code) {
                    out.write(Protocol.encodeMessage(MsgType.PONG,
                        frame.copyOfRange(1, frame.size)))
                    out.flush()
                }
                while (!stop.get()) Thread.sleep(20)
            }
        }

        val stats = StatsSink()
        val client = TransportClient()
        val listener = object : StreamListener {
            override fun onConfig(config: Protocol.Config) {}
            override fun onVideo(video: Protocol.Video) {}
        }
        val tlsTrust = TlsTrust(FakePinStore())
        val clientThread = thread {
            // pingIntervalMs=0 -> ping on the first loop iteration so the test is fast
            client.run("127.0.0.1", port, 640, 480, 320, listener, { !stop.get() },
                stats, 0, tlsTrust = tlsTrust)
        }

        val deadline = System.currentTimeMillis() + 3000
        while (System.currentTimeMillis() < deadline && (stats.rttMs <= 0.0 || stats.fps <= 0.0)) {
            Thread.sleep(20)
        }
        stop.set(true)
        clientThread.join(1000); serverThread.join(1000)

        assertTrue("rtt not measured: ${stats.rttMs}", stats.rttMs > 0.0)
        assertTrue("fps not measured: ${stats.fps}", stats.fps > 0.0)
    }

    private fun beU32(x: Int) = byteArrayOf(
        (x ushr 24).toByte(), (x ushr 16).toByte(), (x ushr 8).toByte(), x.toByte())
    private fun beU64(x: Long) = ByteArray(8) { i -> (x ushr (56 - i * 8)).toByte() }
}
