package com.droppix.app.ui

import android.app.Activity
import android.view.View
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.Switch
import android.widget.TextView
import com.droppix.app.R
import com.droppix.app.settings.AppSettings
import com.droppix.app.settings.Resolutions
import com.droppix.app.settings.SettingsStore

/**
 * In-stream settings overlay.
 *
 * Settings used to live in a separate Activity, so opening them tore the session down and
 * returning rebuilt it — you had to leave the stream to change anything. This drives the
 * same settings over the running stream instead.
 *
 * Two classes of setting, and the difference is real rather than cosmetic:
 *
 *  - **Display params** (flip, brightness, contrast, stats overlay, rotation lock) are local
 *    to this device — shader uniforms and view state — so they apply to the live stream the
 *    instant they change, with no interruption.
 *  - **Stream params** (resolution, fps, quality, audio) are negotiated in HELLO, so the host
 *    can only change them for a NEW session. Those reconnect, which is quick but visible, so
 *    they are grouped separately in the UI and the panel says so rather than appearing to
 *    stall for no reason.
 *
 * Every change is persisted immediately, so a value survives leaving the screen either way.
 */
class StreamSettingsPanel(
    private val activity: Activity,
    private val store: SettingsStore,
    /** Apply a display-only change to the live view. Never restarts the session. */
    private val applyLive: (AppSettings) -> Unit,
    /** Re-negotiate: stop and start the session so HELLO carries the new values. */
    private val reconnect: () -> Unit,
) {
    private val panel: View = activity.findViewById(R.id.settings_panel)
    private val note: TextView = activity.findViewById(R.id.p_note)

    private val flip: Switch = activity.findViewById(R.id.p_flip)
    private val overlay: Switch = activity.findViewById(R.id.p_overlay)
    private val rotLock: Switch = activity.findViewById(R.id.p_rotlock)
    private val bright: SeekBar = activity.findViewById(R.id.p_bright)
    private val contrast: SeekBar = activity.findViewById(R.id.p_contrast)
    private val brightLabel: TextView = activity.findViewById(R.id.p_bright_label)
    private val contrastLabel: TextView = activity.findViewById(R.id.p_contrast_label)

    private val res: Spinner = activity.findViewById(R.id.p_res)
    private val fps: Spinner = activity.findViewById(R.id.p_fps)
    private val quality: Spinner = activity.findViewById(R.id.p_quality)
    private val audio: Switch = activity.findViewById(R.id.p_audio)

    private val fpsValues = listOf(30, 60)
    private val qualityValues = listOf(4000, 8000, 16000)

    /** True while populating the controls, so their listeners don't fire on seeding. */
    private var seeding = false

    val isOpen: Boolean get() = panel.visibility == View.VISIBLE

    init {
        res.adapter = adapter(listOf("Native") + Resolutions.PRESET_HEIGHTS.map { "${it}p" })
        fps.adapter = adapter(fpsValues.map { "$it fps" })
        quality.adapter = adapter(listOf("Low", "Medium", "High"))
        activity.findViewById<Button>(R.id.panel_close).setOnClickListener { hide() }
        wire()
    }

    private fun adapter(items: List<String>) =
        ArrayAdapter(activity, R.layout.spinner_item, items).apply {
            setDropDownViewResource(R.layout.spinner_dropdown_item)
        }

    fun show() {
        seed()
        note.text = ""
        panel.visibility = View.VISIBLE
    }

    fun hide() {
        panel.visibility = View.GONE
    }

    fun toggle() { if (isOpen) hide() else show() }

    /** Load current values into the controls without triggering their listeners. */
    private fun seed() {
        val s = store.load()
        seeding = true
        flip.isChecked = s.flipHorizontal
        overlay.isChecked = s.showOverlay
        rotLock.isChecked = s.rotationLocked
        bright.progress = (s.brightness + 100).coerceIn(0, 200)
        contrast.progress = s.contrast.coerceIn(0, 200)
        res.setSelection(
            if (s.height <= 0) 0
            else 1 + Resolutions.PRESET_HEIGHTS.indexOfFirst { it == s.height }.coerceAtLeast(0))
        fps.setSelection(fpsValues.indexOf(s.fps).coerceAtLeast(0))
        quality.setSelection(qualityValues.indexOf(s.bitrateKbps).coerceAtLeast(0))
        audio.isChecked = s.audio
        labels(s)
        seeding = false
    }

    private fun labels(s: AppSettings) {
        brightLabel.text = "Brightness  ${s.brightness}"
        contrastLabel.text = "Contrast  ${s.contrast}%"
    }

    /** Read every control into a settings snapshot and persist it. */
    private fun collect(): AppSettings {
        val s = store.load().copy(
            // Width is derived from the panel at connect time; only the target height is stored.
            width = 0,
            height = if (res.selectedItemPosition <= 0) 0
                     else Resolutions.PRESET_HEIGHTS[res.selectedItemPosition - 1],
            fps = fpsValues[fps.selectedItemPosition.coerceIn(0, fpsValues.size - 1)],
            audio = audio.isChecked,
            bitrateKbps = qualityValues[quality.selectedItemPosition.coerceIn(0, qualityValues.size - 1)],
            rotationLocked = rotLock.isChecked,
            showOverlay = overlay.isChecked,
            flipHorizontal = flip.isChecked,
            brightness = bright.progress - 100,
            contrast = contrast.progress,
        )
        store.save(s)
        return s
    }

    /** A display-only change: apply to the running stream, no interruption. */
    private fun live() {
        if (seeding) return
        val s = collect()
        labels(s)
        applyLive(s)
    }

    /** A negotiated change: persist, then restart the session so HELLO carries it. */
    private fun renegotiate() {
        if (seeding) return
        val s = collect()
        applyLive(s)
        note.text = "Reconnecting to apply…"
        reconnect()
    }

    private fun wire() {
        flip.setOnCheckedChangeListener { _, _ -> live() }
        overlay.setOnCheckedChangeListener { _, _ -> live() }
        rotLock.setOnCheckedChangeListener { _, _ -> live() }
        bright.setOnSeekBarChangeListener(seek { live() })
        contrast.setOnSeekBarChangeListener(seek { live() })

        audio.setOnCheckedChangeListener { _, _ -> renegotiate() }
        res.onItemSelectedListener = select { renegotiate() }
        fps.onItemSelectedListener = select { renegotiate() }
        quality.onItemSelectedListener = select { renegotiate() }
    }

    private fun seek(onChange: () -> Unit) = object : SeekBar.OnSeekBarChangeListener {
        override fun onProgressChanged(sb: SeekBar?, p: Int, fromUser: Boolean) {
            if (fromUser) onChange()
        }
        override fun onStartTrackingTouch(sb: SeekBar?) {}
        override fun onStopTrackingTouch(sb: SeekBar?) {}
    }

    private fun select(onChange: () -> Unit) =
        object : android.widget.AdapterView.OnItemSelectedListener {
            override fun onItemSelected(
                p: android.widget.AdapterView<*>?, v: View?, pos: Int, id: Long
            ) = onChange()
            override fun onNothingSelected(p: android.widget.AdapterView<*>?) {}
        }
}
