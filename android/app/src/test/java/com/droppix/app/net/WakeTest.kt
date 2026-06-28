package com.droppix.app.net

import org.junit.Assert.*
import org.junit.Test

class WakeTest {
    @Test fun encodeProducesExactBytesForPort27000() {
        val b = Wake.encode(27000)
        assertEquals(6, b.size)
        assertEquals('D'.code.toByte(), b[0])
        assertEquals('P'.code.toByte(), b[1])
        assertEquals('X'.code.toByte(), b[2])
        assertEquals('W'.code.toByte(), b[3])
        assertEquals(0x69.toByte(), b[4])
        assertEquals(0x78.toByte(), b[5])
    }

    @Test fun decodeRoundTrips() {
        val b = Wake.encode(27000)
        val port = Wake.decode(b, b.size)
        assertEquals(27000, port)
    }

    @Test fun decodeRejectsBadMagic() {
        val bad = byteArrayOf('X'.code.toByte(), 'X'.code.toByte(), 'X'.code.toByte(), 'X'.code.toByte(), 0, 0)
        assertNull(Wake.decode(bad, bad.size))
    }

    @Test fun decodeRejectsBadLength() {
        val tooShort = byteArrayOf('D'.code.toByte(), 'P'.code.toByte(), 'X'.code.toByte(), 'W'.code.toByte(), 0)
        assertNull(Wake.decode(tooShort, tooShort.size))
    }
}
