package com.openwave.tinh

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class HandlerProbeTest {
    @Test
    fun encodesSortedPackagesAndTokens() {
        val text = HandlerProbe.encode(mapOf(
            "org.b.mail" to setOf("mailto"),
            "org.a.browser" to setOf("https", "http"),
        ))
        assertEquals("org.a.browser=http,https;org.b.mail=mailto", text)
    }

    @Test
    fun skipsPackagesWithoutHandlers() {
        assertEquals("", HandlerProbe.encode(mapOf("org.a.none" to emptySet())))
    }

    @Test
    fun staysWithinTheSettingLimit() {
        val many = (0 until 5000).associate { "org.example.app$it" to setOf("http", "https") }
        assertTrue(HandlerProbe.encode(many).length <= 60000)
    }

    @Test
    fun probesCoverEveryDefaultCategoryScheme() {
        val tokens = HandlerProbe.probes.map { it.token }.toSet()
        assertEquals(setOf("http", "https", "mailto", "tel", "sms"), tokens)
    }
}
