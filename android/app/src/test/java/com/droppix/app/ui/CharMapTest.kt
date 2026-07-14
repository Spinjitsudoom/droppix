package com.droppix.app.ui
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
class CharMapTest {
    @Test fun letters() {
        assertEquals(30 to false, CharMap.toEvdev('a'))
        assertEquals(30 to true,  CharMap.toEvdev('A'))
        assertEquals(44 to false, CharMap.toEvdev('z'))
        assertEquals(50 to false, CharMap.toEvdev('m'))
    }
    @Test fun digitsAndShiftedDigits() {
        assertEquals(2  to false, CharMap.toEvdev('1'))
        assertEquals(11 to false, CharMap.toEvdev('0'))
        assertEquals(2  to true,  CharMap.toEvdev('!'))
        assertEquals(11 to true,  CharMap.toEvdev(')'))
    }
    @Test fun symbols() {
        assertEquals(53 to false, CharMap.toEvdev('/'))
        assertEquals(53 to true,  CharMap.toEvdev('?'))
        assertEquals(12 to false, CharMap.toEvdev('-'))
        assertEquals(12 to true,  CharMap.toEvdev('_'))
    }
    @Test fun whitespace() {
        assertEquals(57 to false, CharMap.toEvdev(' '))
        assertEquals(28 to false, CharMap.toEvdev('\n'))
        assertEquals(15 to false, CharMap.toEvdev('\t'))
    }
    @Test fun unmapped() {
        assertNull(CharMap.toEvdev('é'))
        assertNull(CharMap.toEvdev('£'))
        assertNull(CharMap.toEvdev('中'))
    }
    @Test fun moreShiftedSymbols() {
        assertEquals(3  to true,  CharMap.toEvdev('@'))   // shift+2
        assertEquals(4  to true,  CharMap.toEvdev('#'))   // shift+3
        assertEquals(26 to true,  CharMap.toEvdev('{'))
        assertEquals(27 to true,  CharMap.toEvdev('}'))
        assertEquals(43 to false, CharMap.toEvdev('\\'))
        assertEquals(43 to true,  CharMap.toEvdev('|'))
        assertEquals(41 to false, CharMap.toEvdev('`'))
        assertEquals(41 to true,  CharMap.toEvdev('~'))
        assertEquals(39 to true,  CharMap.toEvdev(':'))
        assertEquals(40 to true,  CharMap.toEvdev('"'))
    }
}
