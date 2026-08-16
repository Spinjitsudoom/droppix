package com.droppix.app.ui

import android.app.Activity
import android.os.Bundle
import android.text.Editable
import android.text.TextWatcher
import android.view.View
import android.widget.*
import com.droppix.app.R
import com.droppix.app.settings.*

class SettingsActivity : Activity() {
    override fun onCreate(b: Bundle?) {
        super.onCreate(b); setContentView(R.layout.activity_settings)
        val store = SettingsStore(this); val cur = store.load()
        val resSpinner = findViewById<Spinner>(R.id.res_spinner)
        // Presets are target heights; the width follows this device's aspect ratio, so the
        // stream always matches the screen shape instead of being letterboxed or stretched.
        val resItems = listOf("Native") + Resolutions.PRESET_HEIGHTS.map { "${it}p" }
        resSpinner.adapter = lightAdapter(resItems)
        resSpinner.setSelection(
            if (cur.height <= 0) 0
            else 1 + Resolutions.PRESET_HEIGHTS.indexOfFirst { it == cur.height }.coerceAtLeast(0))
        val fpsSpinner = findViewById<Spinner>(R.id.fps_spinner)
        val fpsItems = listOf(30, 60)
        fpsSpinner.adapter = lightAdapter(fpsItems.map { it.toString() })
        fpsSpinner.setSelection(fpsItems.indexOf(cur.fps).coerceAtLeast(0))
        val audioSwitch = findViewById<Switch>(R.id.audio_switch); audioSwitch.isChecked = cur.audio

        val qualitySpinner = findViewById<Spinner>(R.id.quality_spinner)
        val qualityKbps = listOf(4000, 8000, 16000)                 // Low / Medium / High
        val qualityLabels = listOf("Low", "Medium", "High")
        qualitySpinner.adapter = lightAdapter(qualityLabels)
        qualitySpinner.setSelection(qualityKbps.indexOf(cur.bitrateKbps).let { if (it == -1) 1 else it })  // index for the stored bitrate; default Medium only if not one of the presets

        val rotationSpinner = findViewById<Spinner>(R.id.rotation_spinner)
        rotationSpinner.adapter = lightAdapter(listOf("Auto", "Locked"))
        rotationSpinner.setSelection(if (cur.rotationLocked) 1 else 0)

        val overlaySwitch = findViewById<Switch>(R.id.overlay_switch)
        overlaySwitch.isChecked = cur.showOverlay

        val flipSwitch = findViewById<Switch>(R.id.flip_switch)
        flipSwitch.isChecked = cur.flipHorizontal

        val brightnessSeek = findViewById<SeekBar>(R.id.brightness_seek)
        val brightnessVal = findViewById<TextView>(R.id.brightness_val)
        brightnessSeek.progress = cur.brightness + 100                 // stored -100..100 -> 0..200
        brightnessVal.text = cur.brightness.toString()
        val contrastSeek = findViewById<SeekBar>(R.id.contrast_seek)
        val contrastVal = findViewById<TextView>(R.id.contrast_val)
        contrastSeek.progress = cur.contrast                            // stored 0..200
        contrastVal.text = cur.contrast.toString()

        val wallColInput = findViewById<EditText>(R.id.wall_col)
        val wallRowInput = findViewById<EditText>(R.id.wall_row)
        wallColInput.setText(cur.wallCol.toString())
        wallRowInput.setText(cur.wallRow.toString())

        // Auto-save: persist the whole settings snapshot on every control change, so a change
        // survives leaving the screen via Back (previously only the Save button wrote settings).
        fun persist() {
            // Store 0 for "Native", else the chosen target height. Width is derived at connect
            // time from the real panel, so it is not persisted.
            val res = if (resSpinner.selectedItemPosition == 0) 0 to 0
                      else 0 to Resolutions.PRESET_HEIGHTS[resSpinner.selectedItemPosition - 1]
            store.save(AppSettings(
                res.first, res.second,
                fpsItems[fpsSpinner.selectedItemPosition],
                audioSwitch.isChecked,
                qualityKbps[qualitySpinner.selectedItemPosition],
                rotationSpinner.selectedItemPosition == 1,
                overlaySwitch.isChecked,
                flipSwitch.isChecked,
                brightnessSeek.progress - 100,
                contrastSeek.progress,
                wallColInput.text.toString().toIntOrNull() ?: 0,
                wallRowInput.text.toString().toIntOrNull() ?: 0))
        }

        // `ready` gates out the spinners' initial onItemSelected callback (delivered once after
        // onCreate for the programmatic setSelection above) so setup doesn't count as a change.
        var ready = false
        val onSel = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, v: View?, pos: Int, id: Long) { if (ready) persist() }
            override fun onNothingSelected(parent: AdapterView<*>?) {}
        }
        resSpinner.onItemSelectedListener = onSel
        fpsSpinner.onItemSelectedListener = onSel
        qualitySpinner.onItemSelectedListener = onSel
        rotationSpinner.onItemSelectedListener = onSel
        audioSwitch.setOnCheckedChangeListener { _, _ -> if (ready) persist() }
        overlaySwitch.setOnCheckedChangeListener { _, _ -> if (ready) persist() }
        flipSwitch.setOnCheckedChangeListener { _, _ -> if (ready) persist() }
        brightnessSeek.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: SeekBar, p: Int, fromUser: Boolean) { brightnessVal.text = (p - 100).toString() }
            override fun onStartTrackingTouch(sb: SeekBar) {}
            override fun onStopTrackingTouch(sb: SeekBar) { if (ready) persist() }
        })
        contrastSeek.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: SeekBar, p: Int, fromUser: Boolean) { contrastVal.text = p.toString() }
            override fun onStartTrackingTouch(sb: SeekBar) {}
            override fun onStopTrackingTouch(sb: SeekBar) { if (ready) persist() }
        })
        val wallWatcher = object : TextWatcher {
            override fun afterTextChanged(s: Editable?) { if (ready) persist() }
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
        }
        wallColInput.addTextChangedListener(wallWatcher)
        wallRowInput.addTextChangedListener(wallWatcher)

        // Kept as an explicit "done"; auto-save already persisted every change.
        findViewById<Button>(R.id.save_btn).setOnClickListener { persist(); finish() }
        ready = true
    }

    // Spinner adapter with light text in both the collapsed view and the dropdown popup, so
    // items are readable on the dark Settings background / popupBackground.
    private fun lightAdapter(items: List<String>): ArrayAdapter<String> =
        ArrayAdapter(this, R.layout.spinner_item, items).apply {
            setDropDownViewResource(R.layout.spinner_dropdown_item)
        }
}
