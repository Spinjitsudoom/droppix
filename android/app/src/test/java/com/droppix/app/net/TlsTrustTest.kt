package com.droppix.app.net

import org.junit.Assert.*
import org.junit.Test
import java.security.KeyStore
import java.security.cert.X509Certificate
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLServerSocket
import javax.net.ssl.SSLServerSocketFactory
import javax.net.ssl.SSLSocket
import kotlin.concurrent.thread

class TlsTrustTest {
    @Test fun pinLifecycle() {
        val trust = TlsTrust(FakePinStore())
        assertFalse(trust.isPaired("1.2.3.4"))
        assertNull(trust.pinnedFp("1.2.3.4"))

        trust.pin("1.2.3.4", "abc123")
        assertTrue(trust.isPaired("1.2.3.4"))
        assertEquals("abc123", trust.pinnedFp("1.2.3.4"))

        trust.clear("1.2.3.4")
        assertFalse(trust.isPaired("1.2.3.4"))
        assertNull(trust.pinnedFp("1.2.3.4"))
    }

    @Test fun certFingerprintIsStableHexSha256() {
        val cert = loadTestCert()
        val fp = certFingerprint(cert)
        assertEquals(64, fp.length)  // SHA-256 -> 32 bytes -> 64 hex chars
        assertTrue(fp.all { it in "0123456789abcdef" })
        assertEquals(fp, certFingerprint(cert))  // deterministic
    }

    @Test fun socketFactoryCapturesServerCertAndDoesNotThrow() {
        // Spin up a real TLS server using the checked-in test keystore, connect with the
        // SSLSocketFactory built by TlsTrust, and verify the captured cert's fingerprint
        // matches what we get from the keystore directly (i.e. the custom TrustManager ran).
        val expectedCert = loadTestCert()
        val expectedFp = certFingerprint(expectedCert)

        val serverFactory = serverSocketFactory()
        val serverSocket = serverFactory.createServerSocket(0) as SSLServerSocket
        val port = serverSocket.localPort

        val serverThread = thread {
            serverSocket.use {
                val s = it.accept() as SSLSocket
                s.startHandshake()
                s.inputStream.read(ByteArray(16))
                s.close()
            }
        }

        var captured: X509Certificate? = null
        val trust = TlsTrust(FakePinStore())
        val clientFactory = trust.socketFactory { cert -> captured = cert }
        val client = clientFactory.createSocket("127.0.0.1", port) as SSLSocket
        client.startHandshake()
        client.outputStream.write(byteArrayOf(1))
        client.outputStream.flush()
        client.close()

        serverThread.join(2000)

        assertNotNull("trust manager did not capture a server cert", captured)
        assertEquals(expectedFp, certFingerprint(captured!!))
    }

    private fun loadTestCert(): X509Certificate {
        val ks = KeyStore.getInstance("PKCS12")
        javaClass.getResourceAsStream("/test_server.p12").use { ks.load(it, "droppix123".toCharArray()) }
        return ks.getCertificate("droppix-test") as X509Certificate
    }

    private fun serverSocketFactory(): SSLServerSocketFactory {
        val ks = KeyStore.getInstance("PKCS12")
        javaClass.getResourceAsStream("/test_server.p12").use { ks.load(it, "droppix123".toCharArray()) }
        val kmf = javax.net.ssl.KeyManagerFactory.getInstance(javax.net.ssl.KeyManagerFactory.getDefaultAlgorithm())
        kmf.init(ks, "droppix123".toCharArray())
        val context = SSLContext.getInstance("TLS")
        context.init(kmf.keyManagers, null, null)
        return context.serverSocketFactory
    }
}
