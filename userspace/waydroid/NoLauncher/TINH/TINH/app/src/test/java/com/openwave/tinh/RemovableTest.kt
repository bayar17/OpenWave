package com.openwave.tinh

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class RemovableTest {
    @Test
    fun encodesSortedPackages() {
        assertEquals("org.a.one,org.b.two", Removable.encode(listOf("org.b.two", "org.a.one")))
    }

    @Test
    fun emptyListEncodesToNothing() {
        assertEquals("", Removable.encode(emptyList()))
    }

    @Test
    fun staysWithinTheSettingLimit() {
        val many = (0 until 5000).map { "org.example.app$it" }
        assertTrue(Removable.encode(many).length <= 60000)
    }
}
