package com.droppix.app.protocol

import org.junit.Assert.*
import org.junit.Test

class ProtocolTest {
    @Test fun encodeMessageMatchesHostWireFormat() {
        // Must match the C++ host test byte-for-byte:
        // length = 1 (type) + 2 (body) = 3, big-endian; type=VIDEO(3).
        val m = Protocol.encodeMessage(MsgType.VIDEO, byteArrayOf(0xAA.toByte(), 0xBB.toByte()))
        assertArrayEquals(
            byteArrayOf(0, 0, 0, 3, 3, 0xAA.toByte(), 0xBB.toByte()), m)
    }

    @Test fun encodeHelloLayout() {
        val b = Protocol.encodeHello(1, 1920, 1080, 320)
        // 4xu32 header + empty name (u16 len=0) + empty id (u16 len=0) when name/id omitted.
        assertEquals(20, b.size)
        // version=1, width=1920(0x780), height=1080(0x438), density=320(0x140)
        assertArrayEquals(byteArrayOf(0,0,0,1), b.copyOfRange(0,4))
        assertArrayEquals(byteArrayOf(0,0,0x07,0x80.toByte()), b.copyOfRange(4,8))
        assertArrayEquals(byteArrayOf(0,0,0x04,0x38), b.copyOfRange(8,12))
        assertArrayEquals(byteArrayOf(0,0,0x01,0x40), b.copyOfRange(12,16))
        assertArrayEquals(byteArrayOf(0,0), b.copyOfRange(16,18))
        assertArrayEquals(byteArrayOf(0,0), b.copyOfRange(18,20))
    }

    @Test fun encodeHelloV3MatchesHostWireFormat() {
        // version=3,w=1,h=2,density=3, name="ab", id="cd"
        val b = Protocol.encodeHello(3, 1, 2, 3, "ab", "cd")
        // 4xu32 + u16 len + "ab" + u16 len + "cd"
        assertArrayEquals(byteArrayOf(0,0,0,3, 0,0,0,1, 0,0,0,2, 0,0,0,3,
                                      0,2, 'a'.code.toByte(),'b'.code.toByte(),
                                      0,2, 'c'.code.toByte(),'d'.code.toByte()), b)
    }

    @Test fun decodeConfigRoundTrip() {
        // Build a CONFIG body the way the host does: w,h,fps,edlen,extradata.
        val ed = byteArrayOf(0x67, 0x42, 0x00)
        val body = beU32(1920) + beU32(1080) + beU32(30) + beU32(ed.size) + ed
        val c = Protocol.decodeConfig(body)!!
        assertEquals(1920, c.width); assertEquals(1080, c.height); assertEquals(30, c.fps)
        assertArrayEquals(ed, c.extradata)
    }

    @Test fun decodeVideoRoundTrip() {
        val nal = byteArrayOf(0,0,0,1, 0x65, 0x11)
        val body = beU64(123456L) + byteArrayOf(1) + nal  // pts, keyframe=1, nal
        val v = Protocol.decodeVideo(body)!!
        assertEquals(123456L, v.ptsUs); assertTrue(v.keyframe)
        assertArrayEquals(nal, v.nal)
    }

    @Test fun parserReassemblesAcrossPartialFeeds() {
        val m = Protocol.encodeMessage(MsgType.PING, byteArrayOf(1, 2, 3))
        val p = MessageParser()
        p.feed(m.copyOfRange(0, 3), 3)
        assertNull(p.next())
        p.feed(m.copyOfRange(3, m.size), m.size - 3)
        val msg = p.next()!!
        assertEquals(MsgType.PING, msg.type)
        assertArrayEquals(byteArrayOf(1, 2, 3), msg.body)
        assertNull(p.next())
    }

    // big-endian helpers for the tests
    private fun beU32(x: Int) = byteArrayOf(
        (x ushr 24).toByte(), (x ushr 16).toByte(), (x ushr 8).toByte(), x.toByte())
    private fun beU64(x: Long) = ByteArray(8) { i -> (x ushr (56 - i * 8)).toByte() }

    @Test fun encodeInputMatchesHostWireFormat() {
        // action=2, x=0x0102, y=0x0304 ; encodeMessage adds [00 00 00 06][07]
        val m = Protocol.encodeMessage(MsgType.INPUT, Protocol.encodeInput(2, 0x0102, 0x0304))
        assertArrayEquals(
            byteArrayOf(0, 0, 0, 6, 7, 0x02, 0x01, 0x02, 0x03, 0x04), m)
    }

    @Test fun encodeOrientationMatchesHostWireFormat() {
        // code=1 ; encodeMessage adds [00 00 00 02][08]
        val m = Protocol.encodeMessage(MsgType.ORIENTATION, Protocol.encodeOrientation(1))
        assertArrayEquals(byteArrayOf(0, 0, 0, 2, 8, 0x01), m)
    }

    @Test fun audioMessageFramingMatchesHost() {
        val m = Protocol.encodeMessage(MsgType.AUDIO,
            byteArrayOf(0xDE.toByte(), 0xAD.toByte(), 0xBE.toByte(), 0xEF.toByte()))
        assertArrayEquals(
            byteArrayOf(0,0,0,5, 9, 0xDE.toByte(),0xAD.toByte(),0xBE.toByte(),0xEF.toByte()), m)
    }
}
