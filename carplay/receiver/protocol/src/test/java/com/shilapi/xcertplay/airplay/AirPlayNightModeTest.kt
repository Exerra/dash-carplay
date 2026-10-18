package com.shilapi.xcertplay.airplay

import java.io.ByteArrayOutputStream
import java.net.Socket
import org.junit.Assert.*
import org.junit.Test

class AirPlayNightModeTest {
    @Test fun latestQueuedAppearanceIsSentWhenTheEncryptedEventChannelBecomesReady() = Fixture().use { fixture ->
        assertFalse(fixture.session.setNightMode(true))
        assertFalse(fixture.session.setNightMode(false))
        fixture.ready()
        assertAppearance(false, fixture.commands())
        assertTrue(fixture.session.setNightMode(true))
        assertAppearance(true, fixture.commands())
        assertTrue(fixture.session.setNightMode(false))
        assertAppearance(false, fixture.commands())
    }

    @Test fun infoUsesTheCurrentModeForTheInterfaceAndMaps() = Fixture(true).use { fixture ->
        val initial = fixture.info()
        assertEquals(true, initial["nightMode"])
        val initialDisplay = (initial["displays"] as List<*>).single() as Map<*, *>
        assertEquals(1L, initialDisplay["uiAppearanceMode"])
        assertEquals(1L, initialDisplay["mapAppearanceMode"])
        fixture.session.setNightMode(false)
        val info = fixture.info()
        assertEquals(false, info["nightMode"])
        for (display in info["displays"] as List<*>) {
            display as Map<*, *>
            assertEquals(0L, display["uiAppearanceMode"])
            assertEquals(0L, display["mapAppearanceMode"])
            assertEquals(1L, display["uiAppearanceSetting"])
            assertEquals(1L, display["mapAppearanceSetting"])
        }
    }

    @Test fun appearanceAddressesEachConfiguredDisplay() = Fixture(cluster=true).use { fixture ->
        fixture.session.setNightMode(true); fixture.ready()
        val commands = fixture.commands()
        assertEquals(listOf("setNightMode", "uiAppearanceUpdate", "mapAppearanceUpdate",
            "uiAppearanceUpdate", "mapAppearanceUpdate"), commands.map { it["type"] })
        assertEquals(listOf(AirPlayInfoPlist.MAIN_UUID, AirPlayInfoPlist.MAIN_UUID,
            AirPlayInfoPlist.ALT_UUID, AirPlayInfoPlist.ALT_UUID),
            commands.drop(1).map { (it["params"] as Map<*, *>)["uuid"] })
    }

    private fun assertAppearance(night: Boolean, commands: List<Map<*, *>>) {
        assertEquals(listOf("setNightMode", "uiAppearanceUpdate", "mapAppearanceUpdate"), commands.map { it["type"] })
        assertEquals(night, (commands[0]["params"] as Map<*, *>)["nightMode"])
        commands.drop(1).forEach {
            val params = it["params"] as Map<*, *>
            assertEquals(AirPlayInfoPlist.MAIN_UUID, params["uuid"])
            assertEquals(if (night) 1L else 0L, params["appearanceMode"])
            assertEquals(1L, params["appearanceSetting"])
        }
    }

    private class Fixture(night: Boolean = false, cluster: Boolean = false) : AutoCloseable {
        private val output = ByteArrayOutputStream()
        private val key = ByteArray(32) { it.toByte() }
        private val reader = ControlCipher(key, key)
        val session = AirPlaySession(Socket(), AirPlayConfig("test", "02:00:00:00:00:02",
            "02:00:00:00:00:01", "1.0", AirPlayDisplayConfig(800,480),
            cluster=if (cluster) AirPlayDisplayConfig(400,200) else null, nightMode=night),
            AirPlayIdentity.generate(), PairingStore(), null,
            object : AirPlaySessionListener {}, object : AirPlayMediaHandler {})

        fun ready() {
            setField("eventSocket", object : Socket() { override fun getOutputStream() = output })
            setField("eventCipher", ControlCipher(key, key))
            session.javaClass.getDeclaredMethod("sendPendingNightModeLocked").apply { isAccessible=true }.invoke(session)
        }
        private fun setField(name: String, value: Any) {
            session.javaClass.getDeclaredField(name).apply { isAccessible=true }.set(session,value)
        }
        fun commands(): List<Map<*, *>> {
            val plain = reader.decrypt(output.toByteArray()).data; output.reset()
            val parsed = RtspMessage.parseMessages(plain)
            assertTrue(parsed.rest.isEmpty())
            return parsed.messages.map { BplistCodec.decode(it.body) as Map<*, *> }
        }
        fun info(): Map<*, *> {
            val handle = session.javaClass.getDeclaredMethod("handle", RtspMessage.Request::class.java).apply { isAccessible=true }
            val response = handle.invoke(session, RtspMessage.Request("GET", "/info", "RTSP/1.0", emptyMap(), byteArrayOf())) as RtspMessage.Response
            return BplistCodec.decode(response.body) as Map<*, *>
        }
        override fun close() = session.close()
    }
}
