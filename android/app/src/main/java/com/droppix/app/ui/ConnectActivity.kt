package com.droppix.app.ui

import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.text.InputType
import android.view.View
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.ImageButton
import android.widget.ListView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.droppix.app.R
import com.droppix.app.net.Discovery
import com.droppix.app.net.PairingCode
import com.droppix.app.net.TlsTrust
import com.droppix.app.net.certFingerprint
import com.droppix.app.net.WakeService
import com.droppix.app.net.parseQrUri
import com.google.zxing.integration.android.IntentIntegrator
import java.net.InetSocketAddress
import java.security.cert.X509Certificate
import javax.net.ssl.SSLSocket
import kotlin.concurrent.thread

class ConnectActivity : AppCompatActivity() {
    private lateinit var pcList: ListView
    private lateinit var manualAddr: EditText
    private lateinit var connectBtn: Button
    private lateinit var status: TextView
    private lateinit var reconnectBtn: Button
    private lateinit var settingsBtn: Button

    private lateinit var discovery: Discovery
    private lateinit var wakeService: WakeService
    private lateinit var tlsTrust: TlsTrust
    private lateinit var pcListAdapter: ArrayAdapter<String>
    private data class DiscoveredPc(val name: String, val host: String, val port: Int)
    private val discoveredPcs = mutableListOf<DiscoveredPc>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_connect)

        pcList = findViewById(R.id.pc_list)
        manualAddr = findViewById(R.id.manual_addr)
        connectBtn = findViewById(R.id.connect_btn)
        status = findViewById(R.id.status)
        reconnectBtn = findViewById(R.id.reconnect_btn)
        settingsBtn = findViewById(R.id.settings_btn)

        discovery = Discovery(this)
        wakeService = WakeService(this)
        tlsTrust = TlsTrust(this)
        pcListAdapter = ArrayAdapter(this, android.R.layout.simple_list_item_1, mutableListOf<String>())
        pcList.adapter = pcListAdapter
        pcList.setOnItemClickListener { _, _, position, _ ->
            val pc = discoveredPcs.getOrNull(position) ?: return@setOnItemClickListener
            status.text = "Connecting to ${pc.name} (${pc.host}:${pc.port})..."
            connectTo(pc.host, pc.port)
        }

        connectBtn.setOnClickListener { onConnectClicked() }
        findViewById<ImageButton>(R.id.scan_qr_btn).setOnClickListener {
            IntentIntegrator(this)
                .setDesiredBarcodeFormats(IntentIntegrator.QR_CODE)
                .setPrompt("Scan the QR code shown on the PC")
                .setBeepEnabled(true)
                .setOrientationLocked(false)
                .initiateScan()
        }
        findViewById<ImageButton>(R.id.usb_btn).setOnClickListener {
            // Classic USB path: the host runs `adb reverse tcp:27000 tcp:27000`, so the
            // tablet reaches it on localhost. 127.0.0.1 is auto-trusted (no PIN).
            status.text = "Connecting over USB (127.0.0.1:27000)..."
            connectTo("127.0.0.1", 27000)
        }
        updateReconnectRow()
        reconnectBtn.setOnClickListener { onReconnectClicked() }
        settingsBtn.setOnClickListener {
            startActivity(Intent(this, SettingsActivity::class.java))
        }
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        val result = IntentIntegrator.parseActivityResult(requestCode, resultCode, data)
        if (result == null) {
            super.onActivityResult(requestCode, resultCode, data)
            return
        }
        val contents = result.contents
        if (contents == null) {
            status.text = "Scan cancelled"
            return
        }
        val parsed = parseQrUri(contents)
        if (parsed == null) {
            Toast.makeText(this, "Invalid droppix QR code", Toast.LENGTH_SHORT).show()
            return
        }
        status.text = "Pairing with ${parsed.host}:${parsed.port} via QR..."
        connectWithScannedCode(parsed.host, parsed.port, parsed.code)
    }

    /**
     * Host-initiated USB connect.
     *
     * The PC sets up `adb reverse tcp:P tcp:P` and then launches this Activity with these
     * extras, so picking the tablet in the host GUI is the whole interaction — nobody has to
     * pick up the tablet and tap Connect. The port is carried rather than assumed because
     * each monitor gets its own (base..base+3), so a second tablet is not on 27000.
     *
     * Consumed once: removed from the Intent so returning to this screen later (back out of
     * a stream, rotate) does not silently redial.
     */
    private fun handleUsbAutoconnect() {
        if (!intent.getBooleanExtra("usb_autoconnect", false)) return
        val port = intent.getIntExtra("usb_port", 27000)
        intent.removeExtra("usb_autoconnect")
        status.text = "Connecting over USB (127.0.0.1:$port)..."
        connectTo("127.0.0.1", port)
    }

    override fun onNewIntent(newIntent: Intent?) {
        super.onNewIntent(newIntent)
        // The host relaunches an already-running client; without adopting the new Intent the
        // extras below would still be the ones this Activity was first created with.
        newIntent?.let { intent = it }
    }

    override fun onResume() {
        super.onResume()
        handleUsbAutoconnect()
        discovery.start(
            onFound = { name, host, port -> onPcFound(name, host, port) },
            onLost = { name -> onPcLost(name) }
        )
        wakeService.start { host, port ->
            if (com.droppix.app.net.shouldAutoAccept(tlsTrust.isPaired(host))) {
                status.text = "Auto-connecting to $host..."
                connectTo(host, port)
            } else {
                showWakeConfirm(host, port)
            }
        }
    }

    override fun onPause() {
        super.onPause()
        discovery.stop()
        wakeService.stop()
        discoveredPcs.clear()
        refreshPcListAdapter()
    }

    private fun showWakeConfirm(host: String, port: Int) {
        AlertDialog.Builder(this)
            .setTitle("Connect to $host?")
            .setPositiveButton("Connect") { _, _ -> connectTo(host, port) }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun onPcFound(name: String, host: String, port: Int) {
        val idx = discoveredPcs.indexOfFirst { it.name == name }
        val pc = DiscoveredPc(name, host, port)
        if (idx >= 0) {
            discoveredPcs[idx] = pc
        } else {
            discoveredPcs.add(pc)
        }
        refreshPcListAdapter()
    }

    private fun onPcLost(name: String) {
        discoveredPcs.removeAll { it.name == name }
        refreshPcListAdapter()
    }

    private fun refreshPcListAdapter() {
        pcListAdapter.clear()
        pcListAdapter.addAll(discoveredPcs.map { "${it.name} (${it.host}:${it.port})" })
        pcListAdapter.notifyDataSetChanged()
    }

    private fun onConnectClicked() {
        val raw = manualAddr.text.toString().trim()
        if (raw.isEmpty()) {
            status.text = "Enter an address, e.g. 192.168.1.50:27000"
            return
        }
        val (host, port) = parseHostPort(raw)
        if (host.isEmpty()) {
            status.text = "Invalid address: $raw"
            return
        }
        status.text = "Connecting to $host:$port..."
        connectTo(host, port)
    }

    private fun onReconnectClicked() {
        val prefs = getSharedPreferences("droppix", MODE_PRIVATE)
        val lastHost = prefs.getString("last_host", null) ?: return
        val lastPort = prefs.getInt("last_port", 27000)
        status.text = "Reconnecting to $lastHost:$lastPort..."
        connectTo(lastHost, lastPort)
    }

    private fun updateReconnectRow() {
        val prefs = getSharedPreferences("droppix", MODE_PRIVATE)
        val lastHost = prefs.getString("last_host", null)
        if (lastHost == null) {
            reconnectBtn.visibility = View.GONE
        } else {
            reconnectBtn.visibility = View.VISIBLE
            val lastPort = prefs.getInt("last_port", 27000)
            reconnectBtn.text = "Reconnect to $lastHost:$lastPort"
        }
    }

    private fun parseHostPort(raw: String): Pair<String, Int> {
        val idx = raw.lastIndexOf(':')
        if (idx <= 0) return Pair(raw, 27000)
        val host = raw.substring(0, idx)
        val portStr = raw.substring(idx + 1)
        val port = portStr.toIntOrNull() ?: return Pair("", 0)
        return Pair(host, port)
    }

    fun connectTo(host: String, port: Int) {
        when {
            host == "127.0.0.1" -> launchStream(host, port)
            tlsTrust.isPaired(host) -> launchStream(host, port)
            else -> pairThenConnect(host, port)
        }
    }

    private fun connectWithScannedCode(host: String, port: Int, scannedCode: String) {
        when {
            host == "127.0.0.1" -> launchStream(host, port)
            tlsTrust.isPaired(host) -> launchStream(host, port)
            else -> pairThenConnect(host, port, scannedCode)
        }
    }

    private fun launchStream(host: String, port: Int) {
        getSharedPreferences("droppix", MODE_PRIVATE).edit()
            .putString("last_host", host).putInt("last_port", port).apply()
        startActivity(Intent(this, StreamActivity::class.java)
            .putExtra("host", host).putExtra("port", port))
    }

    private fun pairThenConnect(host: String, port: Int, presetCode: String? = null) {
        thread(name = "droppix-pair-probe") {
            var captured: X509Certificate? = null
            var ok = false
            try {
                val socket = tlsTrust.socketFactory { cert -> captured = cert }.createSocket() as SSLSocket
                try {
                    socket.connect(InetSocketAddress(host, port), 5000)
                    socket.startHandshake()
                    ok = true
                } finally {
                    try { socket.close() } catch (_: Exception) {}
                }
            } catch (_: Exception) {
                ok = false
            }
            val cert = captured
            runOnUiThread {
                if (!ok || cert == null) {
                    Toast.makeText(this, "Could not reach $host", Toast.LENGTH_SHORT).show()
                } else if (presetCode != null) {
                    if (presetCode == PairingCode.derive(cert.encoded)) {
                        tlsTrust.pin(host, certFingerprint(cert))
                        Toast.makeText(this, "Paired via QR", Toast.LENGTH_SHORT).show()
                        launchStream(host, port)
                    } else {
                        Toast.makeText(this, "QR code doesn't match this PC — try manual entry", Toast.LENGTH_LONG).show()
                    }
                } else {
                    showPairDialog(host, port, cert)
                }
            }
        }
    }

    private fun showPairDialog(host: String, port: Int, cert: X509Certificate) {
        val input = EditText(this)
        input.inputType = InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_DECIMAL
        input.filters = arrayOf(android.text.InputFilter.LengthFilter(6))

        AlertDialog.Builder(this)
            .setTitle("Pair with $host")
            .setMessage("Enter the 6-digit code shown on the PC")
            .setView(input)
            .setPositiveButton("OK") { _, _ ->
                val entry = input.text.toString().trim()
                if (entry == PairingCode.derive(cert.encoded)) {
                    tlsTrust.pin(host, certFingerprint(cert))
                    launchStream(host, port)
                } else {
                    Toast.makeText(this, "Wrong code", Toast.LENGTH_SHORT).show()
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }
}
