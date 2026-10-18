package com.playport.server

import kotlinx.serialization.json.Json
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ClientInputTest {
    @Test
    fun optionalPressedStateDistinguishesReleaseFromLegacyTap() {
        assertNull(Json.decodeFromString<ClientMessage>("""{"type":"media","media":3}""").pressed)
        val down = Json.decodeFromString<ClientMessage>("""{"type":"knob","knob":{"select":true},"pressed":true}""")
        assertEquals(true, down.pressed); assertEquals(true, down.knob?.select)
        val up = Json.decodeFromString<ClientMessage>("""{"type":"media","media":4,"pressed":false}""")
        assertEquals(false, up.pressed); assertEquals(4, up.media)
    }
}
