package com.droppix.app.net

import java.security.MessageDigest

object PairingCode {
    fun derive(der: ByteArray): String {
        val h = MessageDigest.getInstance("SHA-256").digest(der)
        val u = ((h[0].toLong() and 0xFF) shl 24) or ((h[1].toLong() and 0xFF) shl 16) or
                ((h[2].toLong() and 0xFF) shl 8) or (h[3].toLong() and 0xFF)
        return "%06d".format(u % 1000000L)
    }
}
