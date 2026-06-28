package com.droppix.app.net

/**
 * Codec for the UDP "wake" datagram: ASCII magic `DPXW` (4 bytes) + `u16` port,
 * big-endian = 6 bytes. Used by the PC to summon a tablet; the tablet decodes it
 * to learn which port to dial.
 */
object Wake {
    fun encode(port: Int): ByteArray {
        return byteArrayOf(
            'D'.code.toByte(),
            'P'.code.toByte(),
            'X'.code.toByte(),
            'W'.code.toByte(),
            ((port ushr 8) and 0xFF).toByte(),
            (port and 0xFF).toByte()
        )
    }

    fun decode(b: ByteArray, len: Int): Int? {
        if (len != 6 ||
            b[0] != 'D'.code.toByte() ||
            b[1] != 'P'.code.toByte() ||
            b[2] != 'X'.code.toByte() ||
            b[3] != 'W'.code.toByte()
        ) {
            return null
        }
        return ((b[4].toInt() and 0xFF) shl 8) or (b[5].toInt() and 0xFF)
    }
}
