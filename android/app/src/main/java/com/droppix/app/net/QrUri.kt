package com.droppix.app.net

import java.net.URI

/**
 * Parses a droppix QR code URI and extracts connection parameters.
 * Format: droppix://host:port?code=NNNNNN
 *
 * Examples:
 * - droppix://192.168.1.100:27000?code=123456
 * - droppix://my-pc.local:27000?code=654321
 */
data class QrUri(
    val host: String,
    val port: Int,
    val code: String
)

/**
 * Parses a droppix QR code URI string.
 *
 * @param uri The URI string to parse (e.g., "droppix://host:port?code=NNNNNN")
 * @return Parsed QrUri on success, null if invalid format or missing fields
 */
fun parseQrUri(uri: String): QrUri? {
    return try {
        val parsed = URI(uri)

        // Validate scheme
        if (parsed.scheme != "droppix") return null

        // Extract host and port
        val host = parsed.host ?: return null
        val port = parsed.port
        if (port == -1 || port <= 0 || port > 65535) return null

        // Extract code from query string
        val query = parsed.query ?: return null
        val codeMatch = Regex("(?:^|&)code=([0-9]{6})(?:&|$)").find(query)
        val code = codeMatch?.groupValues?.get(1) ?: return null

        QrUri(host = host, port = port, code = code)
    } catch (e: Exception) {
        null
    }
}
