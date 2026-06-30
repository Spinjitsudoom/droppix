package com.droppix.app.net

import com.droppix.app.protocol.MessageParser
import com.droppix.app.protocol.MsgType
import com.droppix.app.protocol.Protocol
import com.droppix.app.stats.RateMeter
import com.droppix.app.stats.StatsSink
import java.net.InetSocketAddress
import java.security.cert.X509Certificate
import javax.net.ssl.SSLSocket

interface StreamListener {
    fun onConfig(config: Protocol.Config)
    fun onVideo(video: Protocol.Video)
    fun onAudio(pcm: ByteArray) {}
    fun onOverlay(show: Boolean) {}
}

class TransportClient {
    private val sendLock = Any()
    @Volatile private var out: java.io.OutputStream? = null
    // app->host sends originate on the UI thread (touch, orientation); socket I/O there
    // throws NetworkOnMainThreadException. Serialize every send onto one background thread.
    private val sender = java.util.concurrent.Executors.newSingleThreadExecutor { r ->
        Thread(r, "droppix-send").apply { isDaemon = true }
    }
    fun close() { sender.shutdown() }

    private fun longToBytes(x: Long) = ByteArray(8) { i -> (x ushr (56 - i * 8)).toByte() }
    private fun bytesToLong(b: ByteArray): Long {
        var x = 0L; for (i in 0 until 8) x = (x shl 8) or (b[i].toLong() and 0xFF); return x
    }

    // Thread-safe: called from the UI thread while run() reads on the net thread.
    fun sendInput(action: Int, xNorm: Int, yNorm: Int) {
        val o = out ?: return
        val msg = Protocol.encodeMessage(MsgType.INPUT, Protocol.encodeInput(action, xNorm, yNorm))
        submitSend(o, msg)
    }

    fun sendOrientation(code: Int) {
        val o = out ?: return
        val msg = Protocol.encodeMessage(MsgType.ORIENTATION, Protocol.encodeOrientation(code))
        submitSend(o, msg)
    }

    // Encode on the caller; write on the background sender so UI-thread callers don't
    // hit NetworkOnMainThreadException. sendLock still serializes against net-thread writes.
    private fun submitSend(o: java.io.OutputStream, msg: ByteArray) {
        try {
            sender.execute {
                synchronized(sendLock) {
                    try { o.write(msg); o.flush() } catch (_: Exception) { /* dropped; loop will close */ }
                }
            }
        } catch (_: java.util.concurrent.RejectedExecutionException) { /* sender shut down */ }
    }

    fun run(host: String, port: Int, width: Int, height: Int, density: Int,
            listener: StreamListener, isRunning: () -> Boolean,
            stats: StatsSink? = null, pingIntervalMs: Long = 1000,
            name: String = "", id: String = "", tlsTrust: TlsTrust) {
        var serverCert: X509Certificate? = null
        val socket = tlsTrust.socketFactory { cert -> serverCert = cert }.createSocket() as SSLSocket
        try {
            socket.tcpNoDelay = true
            socket.connect(InetSocketAddress(host, port), 5000)
            socket.startHandshake()

            if (host != "127.0.0.1") {
                val cert = serverCert ?: throw IllegalStateException("no server certificate captured")
                val fp = certFingerprint(cert)
                val pinned = tlsTrust.pinnedFp(host)
                when {
                    pinned == null -> throw IllegalStateException("not paired")
                    pinned != fp -> throw CertChangedException(host)
                }
            }

            socket.soTimeout = 1000  // periodic wakeups so isRunning() is checked

            val outStream = socket.getOutputStream()
            out = outStream
            val input = socket.getInputStream()

            synchronized(sendLock) {
                outStream.write(Protocol.encodeMessage(MsgType.HELLO,
                    Protocol.encodeHello(Protocol.VERSION, width, height, density, name, id)))
                outStream.flush()
            }

            val parser = MessageParser()
            val chunk = ByteArray(65536)
            val frameRate = RateMeter(1000)
            var lastPing = 0L
            while (isRunning()) {
                val nowMs = System.currentTimeMillis()
                if (stats != null && nowMs - lastPing >= pingIntervalMs) {
                    synchronized(sendLock) {
                        outStream.write(Protocol.encodeMessage(MsgType.PING, longToBytes(System.nanoTime())))
                        outStream.flush()
                    }
                    lastPing = nowMs
                }
                val n = try { input.read(chunk) } catch (e: java.net.SocketTimeoutException) { 0 }
                if (n > 0) {
                    parser.feed(chunk, n)
                    var msg = parser.next()
                    while (msg != null) {
                        when (msg.type) {
                            MsgType.CONFIG -> Protocol.decodeConfig(msg.body)?.let(listener::onConfig)
                            MsgType.VIDEO -> {
                                Protocol.decodeVideo(msg.body)?.let(listener::onVideo)
                                if (stats != null) {
                                    frameRate.mark(System.currentTimeMillis())
                                    stats.fps = frameRate.ratePerSec(System.currentTimeMillis())
                                }
                            }
                            MsgType.PING -> {
                                val pong = Protocol.encodeMessage(MsgType.PONG, msg.body)
                                synchronized(sendLock) { outStream.write(pong); outStream.flush() }
                            }
                            MsgType.PONG -> if (stats != null && msg.body.size >= 8) {
                                stats.rttMs = (System.nanoTime() - bytesToLong(msg.body)) / 1_000_000.0
                            }
                            MsgType.AUDIO -> listener.onAudio(msg.body)
                            MsgType.OVERLAY -> listener.onOverlay(msg.body.isNotEmpty() && msg.body[0].toInt() != 0)
                            MsgType.BYE -> return
                            else -> { /* ignore */ }
                        }
                        msg = parser.next()
                    }
                } else if (n < 0) {
                    return  // peer closed
                }
            }
        } finally {
            out = null
            try { socket.close() } catch (_: Exception) {}
        }
    }
}
