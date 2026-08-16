package com.droppix.app.net

import android.content.Context
import android.net.nsd.NsdManager
import android.net.wifi.WifiManager
import android.net.nsd.NsdServiceInfo
import android.os.Handler
import android.os.Looper
import android.util.Log
import java.util.ArrayDeque

private const val TAG = "Discovery"
private const val SERVICE_TYPE = "_droppix._tcp"

/**
 * Wraps [NsdManager] to discover droppix hosts advertising `_droppix._tcp` over mDNS.
 *
 * NSD's classic discoverServices/resolveService APIs (used here, not the API 34
 * registerServiceInfoCallback) only allow one resolveService call in flight at a time;
 * calling it again before the previous resolve's listener fires throws. Resolves are
 * therefore serialized through a simple FIFO queue: only one resolve is outstanding,
 * and the next queued resolve is started from the previous resolve's onResolveSucceeded
 * or onResolveFailed callback.
 */
class Discovery(private val ctx: Context) {

    private val nsdManager: NsdManager by lazy {
        ctx.getSystemService(Context.NSD_SERVICE) as NsdManager
    }
    private val mainHandler = Handler(Looper.getMainLooper())

    /**
     * Wi-Fi multicast lock, held only while discovering.
     *
     * mDNS is multicast, and many Wi-Fi drivers (MediaTek in particular) drop multicast
     * frames to save power unless an app holds this lock — discovery then finds nothing at
     * all, with no error anywhere. Released in stop() so it does not cost battery when the
     * connect screen is not open.
     */
    private var multicastLock: WifiManager.MulticastLock? = null

    private fun acquireMulticastLock() {
        if (multicastLock != null) return
        try {
            val wifi = ctx.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
            multicastLock = wifi.createMulticastLock("droppix-nsd").apply {
                setReferenceCounted(false)
                acquire()
            }
        } catch (e: Exception) {
            Log.w(TAG, "multicast lock unavailable: ${e.message}")
        }
    }

    private fun releaseMulticastLock() {
        try { multicastLock?.takeIf { it.isHeld }?.release() } catch (_: Exception) {}
        multicastLock = null
    }

    /**
     * Can this device actually reach [addr]?
     *
     * NSD hands back ONE address, and a host commonly advertises several: loopback, a
     * link-local IPv6, a VPN address (tailscale's 100.64/10), and the real LAN IPv4. Listing
     * an unreachable one gives an entry that fails on tap, which reads as "discovery is
     * broken" rather than "wrong address".
     */
    private fun isReachable(addr: java.net.InetAddress?): Boolean {
        if (addr == null) return false
        if (addr.isLoopbackAddress || addr.isLinkLocalAddress || addr.isAnyLocalAddress) return false
        // 100.64/10 (CGNAT) is what tailscale and similar overlays use; not the LAN.
        val b = addr.address
        if (b.size == 4 && (b[0].toInt() and 0xff) == 100) {
            val second = b[1].toInt() and 0xff
            if (second in 64..127) return false
        }
        return true
    }

    private var discoveryListener: NsdManager.DiscoveryListener? = null
    private var started = false

    private val resolveQueue = ArrayDeque<NsdServiceInfo>()
    private var resolveInFlight = false

    private var onFound: ((name: String, host: String, port: Int) -> Unit)? = null
    private var onLost: ((name: String) -> Unit)? = null

    fun start(
        onFound: (name: String, host: String, port: Int) -> Unit,
        onLost: (name: String) -> Unit
    ) {
        if (started) return
        // Before discoverServices: without the lock the driver may drop the mDNS multicast
        // and we would simply never see a host.
        acquireMulticastLock()
        this.onFound = onFound
        this.onLost = onLost

        val listener = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(serviceType: String) {
                Log.i(TAG, "discovery started for $serviceType")
            }

            override fun onServiceFound(serviceInfo: NsdServiceInfo) {
                Log.i(TAG, "service found: ${serviceInfo.serviceName}")
                mainHandler.post { enqueueResolve(serviceInfo) }
            }

            override fun onServiceLost(serviceInfo: NsdServiceInfo) {
                Log.i(TAG, "service lost: ${serviceInfo.serviceName}")
                val name = serviceInfo.serviceName
                mainHandler.post { this@Discovery.onLost?.invoke(name) }
            }

            override fun onDiscoveryStopped(serviceType: String) {
                Log.i(TAG, "discovery stopped for $serviceType")
            }

            override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) {
                Log.w(TAG, "start discovery failed for $serviceType: error $errorCode")
                started = false
                discoveryListener = null
            }

            override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) {
                Log.w(TAG, "stop discovery failed for $serviceType: error $errorCode")
            }
        }

        discoveryListener = listener
        started = true
        try {
            nsdManager.discoverServices(SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, listener)
        } catch (e: Exception) {
            Log.w(TAG, "discoverServices threw: ${e.message}")
            started = false
            discoveryListener = null
        }
    }

    fun stop() {
        releaseMulticastLock()
        val listener = discoveryListener
        discoveryListener = null
        resolveQueue.clear()
        resolveInFlight = false
        onFound = null
        onLost = null
        if (!started || listener == null) {
            started = false
            return
        }
        started = false
        try {
            nsdManager.stopServiceDiscovery(listener)
        } catch (e: Exception) {
            Log.w(TAG, "stopServiceDiscovery threw: ${e.message}")
        }
    }

    private fun enqueueResolve(serviceInfo: NsdServiceInfo) {
        resolveQueue.add(serviceInfo)
        maybeStartNextResolve()
    }

    private fun maybeStartNextResolve() {
        if (resolveInFlight) return
        val next = resolveQueue.poll() ?: return
        resolveInFlight = true

        val listener = object : NsdManager.ResolveListener {
            override fun onResolveFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {
                Log.w(TAG, "resolve failed for ${serviceInfo.serviceName}: error $errorCode")
                mainHandler.post {
                    resolveInFlight = false
                    maybeStartNextResolve()
                }
            }

            override fun onServiceResolved(serviceInfo: NsdServiceInfo) {
                val host = serviceInfo.host?.hostAddress
                val name = serviceInfo.serviceName
                val port = serviceInfo.port
                mainHandler.post {
                    resolveInFlight = false
                    if (host != null && isReachable(serviceInfo.host)) {
                        this@Discovery.onFound?.invoke(name, host, port)
                    } else {
                        // Loopback / link-local / VPN address: listing it would produce an
                        // entry that fails on tap.
                        Log.w(TAG, "ignoring $name at unreachable address $host")
                    }
                    maybeStartNextResolve()
                }
            }
        }

        try {
            nsdManager.resolveService(next, listener)
        } catch (e: Exception) {
            Log.w(TAG, "resolveService threw: ${e.message}")
            resolveInFlight = false
            maybeStartNextResolve()
        }
    }
}
