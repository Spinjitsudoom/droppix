package com.droppix.app.net

import com.droppix.app.protocol.MsgType
import com.droppix.app.protocol.Protocol
import org.junit.Assert.*
import org.junit.Test
import java.io.DataInputStream
import java.security.KeyStore
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLServerSocket
import javax.net.ssl.SSLServerSocketFactory
import kotlin.concurrent.thread

// A loopback address distinct from "127.0.0.1" so the TransportClient pin-check branch (not exempt) runs.
private const val PIN_TEST_HOST = "127.0.0.2"

private fun testServerSocketFactory(): SSLServerSocketFactory {
    val ks = KeyStore.getInstance("PKCS12")
    TransportClientTest::class.java.getResourceAsStream("/test_server.p12").use {
        ks.load(it, "droppix123".toCharArray())
    }
    val kmf = javax.net.ssl.KeyManagerFactory.getInstance(javax.net.ssl.KeyManagerFactory.getDefaultAlgorithm())
    kmf.init(ks, "droppix123".toCharArray())
    val context = SSLContext.getInstance("TLS")
    context.init(kmf.keyManagers, null, null)
    return context.serverSocketFactory
}

class TransportClientTest {
    @Test fun handshakeReceivesConfigAndVideo() {
        val server = testServerSocketFactory().createServerSocket(0) as SSLServerSocket
        val port = server.localPort

        // Fake host: accept, read HELLO, send CONFIG + one VIDEO.
        val serverThread = thread {
            server.use {
                val sock = it.accept() as javax.net.ssl.SSLSocket
                sock.startHandshake()
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
        val tlsTrust = TlsTrust(FakePinStore())
        val clientThread = thread {
            client.run("127.0.0.1", port, 1920, 1080, 320, listener, { true }, tlsTrust = tlsTrust)
        }

        assertTrue("did not receive config+video", latch.await(3, TimeUnit.SECONDS))
        assertEquals(1920, gotConfig!!.width)
        assertEquals(1000L, gotVideo!!.ptsUs)
        assertTrue(gotVideo!!.keyframe)

        serverThread.join(1000); clientThread.join(1000)
    }

    @Test fun nonLocalhostWithoutPinThrows() {
        val server = testServerSocketFactory().createServerSocket(0, 50, java.net.InetAddress.getByName(PIN_TEST_HOST)) as SSLServerSocket
        val port = server.localPort
        val serverThread = thread {
            server.use {
                val sock = it.accept() as javax.net.ssl.SSLSocket
                try { sock.startHandshake() } catch (_: Exception) {}
            }
        }

        val client = TransportClient()
        val tlsTrust = TlsTrust(FakePinStore())  // no pin recorded for PIN_TEST_HOST
        val listener = object : StreamListener {
            override fun onConfig(config: Protocol.Config) {}
            override fun onVideo(video: Protocol.Video) {}
        }

        try {
            assertThrows(IllegalStateException::class.java) {
                client.run(PIN_TEST_HOST, port, 100, 100, 320, listener, { true }, tlsTrust = tlsTrust)
            }
        } finally {
            serverThread.join(1000)
        }
    }

    @Test fun nonLocalhostWithStalePinThrowsCertChanged() {
        val server = testServerSocketFactory().createServerSocket(0, 50, java.net.InetAddress.getByName(PIN_TEST_HOST)) as SSLServerSocket
        val port = server.localPort
        val serverThread = thread {
            server.use {
                val sock = it.accept() as javax.net.ssl.SSLSocket
                try { sock.startHandshake() } catch (_: Exception) {}
            }
        }

        val client = TransportClient()
        val tlsTrust = TlsTrust(FakePinStore())
        tlsTrust.pin(PIN_TEST_HOST, "0".repeat(64))  // deliberately wrong fingerprint
        val listener = object : StreamListener {
            override fun onConfig(config: Protocol.Config) {}
            override fun onVideo(video: Protocol.Video) {}
        }

        try {
            assertThrows(CertChangedException::class.java) {
                client.run(PIN_TEST_HOST, port, 100, 100, 320, listener, { true }, tlsTrust = tlsTrust)
            }
        } finally {
            serverThread.join(1000)
        }
    }

    @Test fun nonLocalhostWithMatchingPinSucceeds() {
        val server = testServerSocketFactory().createServerSocket(0, 50, java.net.InetAddress.getByName(PIN_TEST_HOST)) as SSLServerSocket
        val port = server.localPort
        val serverThread = thread {
            server.use {
                val sock = it.accept() as javax.net.ssl.SSLSocket
                sock.startHandshake()
                val input = DataInputStream(sock.getInputStream())
                val len = input.readInt(); input.readFully(ByteArray(len))
                sock.close()
            }
        }

        val ks = KeyStore.getInstance("PKCS12")
        TransportClientTest::class.java.getResourceAsStream("/test_server.p12").use {
            ks.load(it, "droppix123".toCharArray())
        }
        val cert = ks.getCertificate("droppix-test") as java.security.cert.X509Certificate
        val correctFp = certFingerprint(cert)

        val client = TransportClient()
        val tlsTrust = TlsTrust(FakePinStore())
        tlsTrust.pin(PIN_TEST_HOST, correctFp)
        val listener = object : StreamListener {
            override fun onConfig(config: Protocol.Config) {}
            override fun onVideo(video: Protocol.Video) {}
        }

        // Should not throw — pin matches.
        client.run(PIN_TEST_HOST, port, 100, 100, 320, listener, { false }, tlsTrust = tlsTrust)
        serverThread.join(1000)
    }

    private fun beU32(x: Int) = byteArrayOf(
        (x ushr 24).toByte(), (x ushr 16).toByte(), (x ushr 8).toByte(), x.toByte())
    private fun beU64(x: Long) = ByteArray(8) { i -> (x ushr (56 - i * 8)).toByte() }
}
