package com.droppix.app.net

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AoaAccessoryTest {
    @Test fun matchesTheStringsTheHostSends() =
        assertTrue(isDroppixAccessory("droppix", "droppix"))

    @Test fun rejectsOtherAccessories() {
        assertFalse(isDroppixAccessory("Google, Inc.", "DemoKit"))
        assertFalse(isDroppixAccessory("droppix", "other"))
    }

    @Test fun rejectsMissingStrings() {
        assertFalse(isDroppixAccessory(null, "droppix"))
        assertFalse(isDroppixAccessory("droppix", null))
    }
}
