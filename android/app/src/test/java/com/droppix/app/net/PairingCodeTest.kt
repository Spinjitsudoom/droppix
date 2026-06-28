package com.droppix.app.net

import org.junit.Assert.*
import org.junit.Test

class PairingCodeTest {
    @Test fun deriveMatchesHostLockedValue() {
        val der = byteArrayOf(0xDE.toByte(), 0xAD.toByte(), 0xBE.toByte(), 0xEF.toByte(), 1, 2, 3, 4)
        val code = PairingCode.derive(der)
        assertEquals(6, code.length)
        assertTrue(code.all { it in '0'..'9' })
        assertEquals(code, PairingCode.derive(der))
        // Same literal locked in host/tests/test_pairing_code.cpp — proves byte-identity.
        assertEquals("376946", code)
    }
}
