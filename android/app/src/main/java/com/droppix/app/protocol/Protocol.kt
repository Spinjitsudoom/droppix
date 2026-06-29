package com.droppix.app.protocol

enum class MsgType(val code: Int) {
    HELLO(1), CONFIG(2), VIDEO(3), PING(4), PONG(5), BYE(6), INPUT(7), ORIENTATION(8), AUDIO(9);
    companion object {
        fun fromCode(c: Int): MsgType? = entries.firstOrNull { it.code == c }
    }
}

data class ParsedMessage(val type: MsgType, val body: ByteArray)

object Protocol {
    const val VERSION = 2

    private fun putU32(out: ArrayList<Byte>, x: Int) {
        out.add((x ushr 24).toByte()); out.add((x ushr 16).toByte())
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
                    name: String = "", id: String = ""): ByteArray {
        val out = ArrayList<Byte>()
        putU32(out, version); putU32(out, width); putU32(out, height); putU32(out, density)
        val n = name.toByteArray(Charsets.UTF_8); val i = id.toByteArray(Charsets.UTF_8)
        out.add((n.size ushr 8).toByte()); out.add(n.size.toByte()); for (x in n) out.add(x)
        out.add((i.size ushr 8).toByte()); out.add(i.size.toByte()); for (x in i) out.add(x)
        return out.toByteArray()
    }

    // INPUT body: u8 action (0=down,1=move,2=up), u16 x_norm, u16 y_norm (big-endian).
    fun encodeInput(action: Int, xNorm: Int, yNorm: Int): ByteArray {
        val out = ArrayList<Byte>(5)
        out.add(action.toByte())
        out.add((xNorm ushr 8).toByte()); out.add(xNorm.toByte())
        out.add((yNorm ushr 8).toByte()); out.add(yNorm.toByte())
        return out.toByteArray()
    }

    // ORIENTATION body: u8 code (0=0°, 1=90°, 2=180°, 3=270°).
    fun encodeOrientation(code: Int): ByteArray = byteArrayOf(code.toByte())

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
