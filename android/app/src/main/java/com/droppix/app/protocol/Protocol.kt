package com.droppix.app.protocol

enum class MsgType(val code: Int) {
    HELLO(1), CONFIG(2), VIDEO(3), PING(4), PONG(5), BYE(6), INPUT(7), ORIENTATION(8),
    AUDIO(9), OVERLAY(10), TOUCH(11), SCROLL(12), MOUSE_BUTTON(13), KEY(14), PEN(15),
    // client -> host: "my decoder lost sync, send an IDR now". Without it, a dropped
    // delta leaves the picture corrupt until the host's next scheduled keyframe (~2s).
    KEYFRAME_REQUEST(16),
    // client -> host: change fps / bitrate / audio on the RUNNING session, so a settings
    // change applies without tearing the stream down. Body: u32 fps, u32 kbps, u8 audio.
    STREAM_PARAMS(17);
    companion object {
        fun fromCode(c: Int): MsgType? = entries.firstOrNull { it.code == c }
    }
}

data class ParsedMessage(val type: MsgType, val body: ByteArray)

// One finger in a multi-touch report: id (stable across a gesture), x/y in 0..65535 across
// the display, pressure in 0..1023.
data class Contact(val id: Int, val x: Int, val y: Int, val pressure: Int)

object Protocol {
    const val VERSION = 6

    private fun putU32(out: ArrayList<Byte>, x: Int) {
        out.add((x ushr 24).toByte()); out.add((x ushr 16).toByte())
        out.add((x ushr 8).toByte()); out.add(x.toByte())
    }
    private fun putU16(out: ArrayList<Byte>, x: Int) {
        out.add((x ushr 8).toByte()); out.add(x.toByte())
    }
    private fun getU32(b: ByteArray, o: Int): Int =
        ((b[o].toInt() and 0xFF) shl 24) or ((b[o+1].toInt() and 0xFF) shl 16) or
        ((b[o+2].toInt() and 0xFF) shl 8) or (b[o+3].toInt() and 0xFF)
    private fun getU64(b: ByteArray, o: Int): Long {
        var x = 0L
        for (i in 0 until 8) x = (x shl 8) or (b[o + i].toLong() and 0xFF)
        return x
    }

    fun encodeMessage(type: MsgType, body: ByteArray): ByteArray {
        val out = ArrayList<Byte>(5 + body.size)
        putU32(out, 1 + body.size)            // length covers type + body
        out.add(type.code.toByte())
        for (b in body) out.add(b)
        return out.toByteArray()
    }

    fun encodeHello(version: Int, width: Int, height: Int, density: Int,
                    name: String = "", id: String = "",
                    fps: Int = 0, audioWanted: Int = 0, orientationCode: Int = 0, bitrateKbps: Int = 0,
                    wallCol: Int = 0, wallRow: Int = 0): ByteArray {
        val out = ArrayList<Byte>()
        putU32(out, version); putU32(out, width); putU32(out, height); putU32(out, density)
        if (version >= 4) {
            putU32(out, fps); out.add(audioWanted.toByte()); out.add(orientationCode.toByte())
        }
        if (version >= 5) { putU32(out, bitrateKbps) }
        if (version >= 6) { putU16(out, wallCol); putU16(out, wallRow) }
        val n = name.toByteArray(Charsets.UTF_8); val i = id.toByteArray(Charsets.UTF_8)
        out.add((n.size ushr 8).toByte()); out.add(n.size.toByte()); for (x in n) out.add(x)
        out.add((i.size ushr 8).toByte()); out.add(i.size.toByte()); for (x in i) out.add(x)
        return out.toByteArray()
    }

    // INPUT body: u8 action (0=down,1=move,2=up), u16 x_norm, u16 y_norm, u16 pressure (big-endian).
    fun encodeInput(action: Int, xNorm: Int, yNorm: Int, pressure: Int): ByteArray {
        val out = ArrayList<Byte>(7)
        out.add(action.toByte())
        out.add((xNorm ushr 8).toByte()); out.add(xNorm.toByte())
        out.add((yNorm ushr 8).toByte()); out.add(yNorm.toByte())
        out.add((pressure ushr 8).toByte()); out.add(pressure.toByte())
        return out.toByteArray()
    }

    // TOUCH body: u8 count, then count x { u8 id, u16 x, u16 y, u16 pressure } (big-endian).
    // The full set of active contacts each event (count 0 = all up); capped at 10.
    fun encodeTouch(contacts: List<Contact>): ByteArray {
        val n = minOf(contacts.size, 10)
        val out = ArrayList<Byte>(1 + n * 7)
        out.add(n.toByte())
        for (i in 0 until n) {
            val c = contacts[i]
            out.add(c.id.toByte())
            out.add((c.x ushr 8).toByte()); out.add(c.x.toByte())
            out.add((c.y ushr 8).toByte()); out.add(c.y.toByte())
            out.add((c.pressure ushr 8).toByte()); out.add(c.pressure.toByte())
        }
        return out.toByteArray()
    }

    // ORIENTATION body: u8 code (0=0°, 1=90°, 2=180°, 3=270°).
    fun encodeOrientation(code: Int): ByteArray = byteArrayOf(code.toByte())

    // SCROLL body: i16 dx, i16 dy, u16 x, u16 y (big-endian).
    fun encodeScroll(dx: Int, dy: Int, x: Int, y: Int): ByteArray {
        val out = ArrayList<Byte>(8)
        fun u16(v: Int) { out.add((v ushr 8).toByte()); out.add(v.toByte()) }
        u16(dx); u16(dy); u16(x); u16(y)
        return out.toByteArray()
    }

    // MOUSE_BUTTON body: u8 button, u8 action, u16 x, u16 y (big-endian).
    fun encodeMouseButton(button: Int, action: Int, x: Int, y: Int): ByteArray {
        val out = ArrayList<Byte>(6)
        out.add(button.toByte()); out.add(action.toByte())
        out.add((x ushr 8).toByte()); out.add(x.toByte())
        out.add((y ushr 8).toByte()); out.add(y.toByte())
        return out.toByteArray()
    }

    // STREAMPARAMS body: u32 fps, u32 bitrate_kbps, u8 audio_wanted (big-endian).
    fun encodeStreamParams(fps: Int, bitrateKbps: Int, audioWanted: Int): ByteArray {
        val b = ByteArray(9)
        for (i in 0..3) b[i] = ((fps shr (8 * (3 - i))) and 0xff).toByte()
        for (i in 0..3) b[4 + i] = ((bitrateKbps shr (8 * (3 - i))) and 0xff).toByte()
        b[8] = audioWanted.toByte()
        return b
    }

    // KEY body: u16 keycode (big-endian), u8 action (0=up,1=down,2=repeat).
    fun encodeKey(keycode: Int, action: Int): ByteArray =
        byteArrayOf((keycode ushr 8).toByte(), keycode.toByte(), action.toByte())

    // PEN body: u16 x, u16 y, u16 pressure (big-endian), u8 flags (bit0=touching, bit1=eraser).
    fun encodePen(x: Int, y: Int, pressure: Int, flags: Int): ByteArray =
        byteArrayOf((x ushr 8).toByte(), x.toByte(), (y ushr 8).toByte(), y.toByte(),
                    (pressure ushr 8).toByte(), pressure.toByte(), flags.toByte())

    data class Config(val width: Int, val height: Int, val fps: Int, val extradata: ByteArray)
    fun decodeConfig(body: ByteArray): Config? {
        if (body.size < 16) return null
        val w = getU32(body, 0); val h = getU32(body, 4); val fps = getU32(body, 8)
        val n = getU32(body, 12)
        if (body.size != 16 + n) return null
        return Config(w, h, fps, body.copyOfRange(16, body.size))
    }

    data class Video(val ptsUs: Long, val keyframe: Boolean, val nal: ByteArray)
    fun decodeVideo(body: ByteArray): Video? {
        if (body.size < 9) return null
        val pts = getU64(body, 0)
        val key = body[8].toInt() != 0
        return Video(pts, key, body.copyOfRange(9, body.size))
    }
}

class MessageParser {
    private var buf = ByteArray(0)
    private var pos = 0
    private val maxMessage = 64 * 1024 * 1024  // mirror host sanity cap

    fun feed(data: ByteArray, n: Int) {
        buf = buf.plus(data.copyOfRange(0, n))  // buf.plus already allocates a fresh array
    }

    fun next(): ParsedMessage? {
        while (true) {
            if (buf.size - pos < 4) return null
            val len = ((buf[pos].toInt() and 0xFF) shl 24) or
                      ((buf[pos+1].toInt() and 0xFF) shl 16) or
                      ((buf[pos+2].toInt() and 0xFF) shl 8) or
                      (buf[pos+3].toInt() and 0xFF)
            if (len < 1 || len > maxMessage) { pos += 4; continue }
            if (buf.size - pos < 4 + len) return null
            val type = MsgType.fromCode(buf[pos + 4].toInt() and 0xFF)
            val body = buf.copyOfRange(pos + 5, pos + 4 + len)
            pos += 4 + len
            if (pos > 65536) { buf = buf.copyOfRange(pos, buf.size); pos = 0 }
            if (type == null) continue   // unknown type: skip
            return ParsedMessage(type, body)
        }
    }
}
