package com.droppix.app.net

import android.content.Context
import java.io.IOException
import java.security.MessageDigest
import java.security.SecureRandom
import java.security.cert.X509Certificate
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLSocketFactory
import javax.net.ssl.X509TrustManager

/** Thrown when a peer's pinned certificate fingerprint changes (host re-imaged, MITM, etc.). */
class CertChangedException(host: String) : IOException("certificate for $host changed from pinned value")

/** Lowercase hex SHA-256 of the certificate's DER encoding — the value we pin. */
fun certFingerprint(cert: X509Certificate): String {
    val digest = MessageDigest.getInstance("SHA-256").digest(cert.encoded)
    val sb = StringBuilder(digest.size * 2)
    for (b in digest) sb.append("%02x".format(b))
    return sb.toString()
}

/** Minimal key-value seam so [TlsTrust]'s pin logic is unit-testable without a real Android Context. */
internal interface PinStore {
    fun get(host: String): String?
    fun put(host: String, fp: String)
    fun remove(host: String)
}

private class SharedPrefsPinStore(ctx: Context) : PinStore {
    private val prefs = ctx.getSharedPreferences("droppix_pins", Context.MODE_PRIVATE)
    override fun get(host: String): String? = prefs.getString(host, null)
    override fun put(host: String, fp: String) { prefs.edit().putString(host, fp).apply() }
    override fun remove(host: String) { prefs.edit().remove(host).apply() }
}

/**
 * Stores per-host pinned certificate fingerprints and builds an [SSLSocketFactory] that performs
 * NO certificate authority validation — the server's cert is captured via callback and the caller
 * decides whether to trust it (by comparing against the pin). This is intentional: the host's TLS
 * cert is self-signed, so trust comes entirely from the user-confirmed PIN pairing flow, not a CA.
 */
class TlsTrust internal constructor(private val store: PinStore) {
    constructor(ctx: Context) : this(SharedPrefsPinStore(ctx))

    fun isPaired(host: String): Boolean = store.get(host) != null

    fun pinnedFp(host: String): String? = store.get(host)

    fun pin(host: String, fp: String) {
        store.put(host, fp)
    }

    fun clear(host: String) {
        store.remove(host)
    }

    /**
     * Builds an SSLSocketFactory whose trust manager accepts any server certificate (no CA chain
     * validation) but reports the leaf certificate to [captured] so the caller can pin/verify it.
     */
    fun socketFactory(captured: (cert: X509Certificate) -> Unit): SSLSocketFactory {
        val trustManager = object : X509TrustManager {
            override fun checkClientTrusted(chain: Array<out X509Certificate>, authType: String) {
                // no-op: we don't authenticate clients
            }

            override fun checkServerTrusted(chain: Array<out X509Certificate>, authType: String) {
                captured(chain[0])
                // Return without throwing: trust is decided by the caller via pin comparison.
            }

            override fun getAcceptedIssuers(): Array<X509Certificate> = arrayOf()
        }

        val context = SSLContext.getInstance("TLS")
        context.init(null, arrayOf(trustManager), SecureRandom())
        return context.socketFactory
    }
}
