package com.droppix.app.net

/** In-memory [PinStore] so tests don't need a real Android Context/SharedPreferences. */
internal class FakePinStore : PinStore {
    private val map = HashMap<String, String>()
    override fun get(host: String): String? = map[host]
    override fun put(host: String, fp: String) { map[host] = fp }
    override fun remove(host: String) { map.remove(host) }
}
