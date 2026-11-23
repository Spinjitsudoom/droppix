package com.droppix.app.net

// Must match res/xml/accessory_filter.xml and the strings the host sends in aoa_connect.cpp.
const val AOA_MANUFACTURER = "droppix"
const val AOA_MODEL = "droppix"

fun isDroppixAccessory(manufacturer: String?, model: String?): Boolean =
    manufacturer == AOA_MANUFACTURER && model == AOA_MODEL
