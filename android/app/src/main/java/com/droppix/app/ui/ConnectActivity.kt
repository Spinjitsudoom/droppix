package com.droppix.app.ui

import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.ListView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.droppix.app.R

class ConnectActivity : AppCompatActivity() {
    private lateinit var pcList: ListView
    private lateinit var manualAddr: EditText
    private lateinit var connectBtn: Button
    private lateinit var status: TextView
    private lateinit var reconnectBtn: Button

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_connect)

        pcList = findViewById(R.id.pc_list)
        manualAddr = findViewById(R.id.manual_addr)
        connectBtn = findViewById(R.id.connect_btn)
        status = findViewById(R.id.status)
        reconnectBtn = findViewById(R.id.reconnect_btn)

        // Placeholder: discovered-PCs list is populated by Task 8 (network discovery).
        pcList.adapter = ArrayAdapter(this, android.R.layout.simple_list_item_1, emptyList<String>())

        connectBtn.setOnClickListener { onConnectClicked() }
        updateReconnectRow()
        reconnectBtn.setOnClickListener { onReconnectClicked() }
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
        getSharedPreferences("droppix", MODE_PRIVATE).edit()
            .putString("last_host", host).putInt("last_port", port).apply()
        startActivity(Intent(this, StreamActivity::class.java)
            .putExtra("host", host).putExtra("port", port))
    }
}
