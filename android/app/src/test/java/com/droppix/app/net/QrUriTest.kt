package com.droppix.app.net

import org.junit.Assert.*
import org.junit.Test

class QrUriTest {
    @Test fun parseValidIpUri() {
        val result = parseQrUri("droppix://192.168.1.100:27000?code=123456")
        assertNotNull(result)
        assertEquals("192.168.1.100", result?.host)
        assertEquals(27000, result?.port)
        assertEquals("123456", result?.code)
    }

    @Test fun parseValidHostnameUri() {
        val result = parseQrUri("droppix://my-pc.local:27000?code=654321")
        assertNotNull(result)
        assertEquals("my-pc.local", result?.host)
        assertEquals(27000, result?.port)
        assertEquals("654321", result?.code)
    }

    @Test fun parseValidNonStandardPort() {
        val result = parseQrUri("droppix://localhost:27001?code=111111")
        assertNotNull(result)
        assertEquals("localhost", result?.host)
        assertEquals(27001, result?.port)
        assertEquals("111111", result?.code)
    }

    @Test fun rejectMissingScheme() {
        val result = parseQrUri("http://192.168.1.100:27000?code=123456")
        assertNull(result)
    }

    @Test fun rejectMissingHost() {
        val result = parseQrUri("droppix://:27000?code=123456")
        assertNull(result)
    }

    @Test fun rejectMissingPort() {
        val result = parseQrUri("droppix://192.168.1.100?code=123456")
        assertNull(result)
    }

    @Test fun rejectInvalidPort() {
        val result = parseQrUri("droppix://192.168.1.100:99999?code=123456")
        assertNull(result)
    }

    @Test fun rejectMissingCode() {
        val result = parseQrUri("droppix://192.168.1.100:27000")
        assertNull(result)
    }

    @Test fun rejectInvalidCodeFormat() {
        val result = parseQrUri("droppix://192.168.1.100:27000?code=12345")  // 5 digits
        assertNull(result)
    }

    @Test fun rejectNonNumericCode() {
        val result = parseQrUri("droppix://192.168.1.100:27000?code=12345a")
        assertNull(result)
    }

    @Test fun parseCodeWithOtherQueryParams() {
        val result = parseQrUri("droppix://192.168.1.100:27000?v=1&code=123456&x=y")
        assertNotNull(result)
        assertEquals("123456", result?.code)
    }

    @Test fun rejectMalformedUri() {
        val result = parseQrUri("not a uri at all")
        assertNull(result)
    }
}
