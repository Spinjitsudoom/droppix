package com.droppix.app.net

import com.droppix.app.protocol.MessageParser
import com.droppix.app.protocol.MsgType
import com.droppix.app.protocol.Protocol
import java.net.InetSocketAddress
import java.net.Socket

interface StreamListener {
    fun onConfig(config: Protocol.Config)
    fun onVideo(video: Protocol.Video)
}

class TransportClient {
    fun run(host: String, port: Int, width: Int, height: Int, density: Int,
            listener: StreamListener, isRunning: () -> Boolean) {
        val socket = Socket()
        socket.tcpNoDelay = true
        socket.connect(InetSocketAddress(host, port), 5000)
        socket.soTimeout = 1000  // periodic wakeups so isRunning() is checked

        val out = socket.getOutputStream()
        val input = socket.getInputStream()

        out.write(Protocol.encodeMessage(MsgType.HELLO,
            Protocol.encodeHello(Protocol.VERSION, width, height, density)))
        out.flush()

        val parser = MessageParser()
        val chunk = ByteArray(65536)
        try {
            while (isRunning()) {
                val n = try { input.read(chunk) } catch (e: java.net.SocketTimeoutException) { 0 }
                if (n > 0) {
                    parser.feed(chunk, n)
                    var msg = parser.next()
                    while (msg != null) {
                        when (msg.type) {
                            MsgType.CONFIG -> Protocol.decodeConfig(msg.body)?.let(listener::onConfig)
                            MsgType.VIDEO -> Protocol.decodeVideo(msg.body)?.let(listener::onVideo)
                            MsgType.PING -> { out.write(Protocol.encodeMessage(MsgType.PONG, msg.body)); out.flush() }
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
            try { socket.close() } catch (_: Exception) {}
        }
    }
}
