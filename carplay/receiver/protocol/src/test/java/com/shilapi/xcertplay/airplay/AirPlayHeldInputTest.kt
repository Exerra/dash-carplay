package com.shilapi.xcertplay.airplay

import java.io.ByteArrayOutputStream
import java.net.Socket
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test

class AirPlayHeldInputTest {
    @Test
    fun heldKnobStatesDoNotGetAnImmediateReleaseButLegacyTapsStillDo() = Fixture().use { fixture ->
        fixture.session.sendKnob(AirPlayKnobState(select = true, x = 127), momentary = false)
        fixture.session.sendKnob(AirPlayKnobState(select = true), momentary = false)
        fixture.session.sendKnob(AirPlayKnobState(), momentary = false)
        fixture.session.sendKnob(AirPlayKnobState(home = true))
        val commands = fixture.commands()
        assertEquals(5, commands.size)
        assertEquals(AirPlayHid.KNOB_HID_UID.toString(16), commands[0]["uuid"])
        listOf(byteArrayOf(1,127,0,0), byteArrayOf(1,0,0,0), byteArrayOf(0,0,0,0),
            byteArrayOf(2,0,0,0), byteArrayOf(0,0,0,0)).forEachIndexed { index, report ->
            assertArrayEquals(report, commands[index]["hidReport"] as ByteArray)
        }
    }

    @Test
    fun mediaAndCallButtonsSendDistinctDownAndUpReportsAndKeepTapCompatibility() = Fixture().use { fixture ->
        fixture.session.sendMedia(CarPlayMediaButton.NEXT, true)
        fixture.session.sendMedia(CarPlayMediaButton.NEXT, false)
        fixture.session.sendMedia(CarPlayMediaButton.PLAY_PAUSE)
        fixture.session.sendTelephony(3, true)
        fixture.session.sendTelephony(3, false)
        fixture.session.sendTelephony(1)
        val commands = fixture.commands()
        assertEquals(8, commands.size)
        val values = listOf(4,0,3,0,3,0,1,0)
        commands.forEachIndexed { index, command ->
            val uid = if (index < 4) AirPlayHid.MEDIA_HID_UID else AirPlayHid.TELEPHONY_HID_UID
            assertEquals(uid.toString(16), command["uuid"])
            assertArrayEquals(byteArrayOf(values[index].toByte()), command["hidReport"] as ByteArray)
        }
    }

    @Test
    fun siriUsesTheActualPressDurationAndLegacyTapStillSendsBothEdges() = Fixture().use { fixture ->
        fixture.session.setSiriPressed(true)
        assertEquals(listOf(2L), fixture.commands().map { (it["params"] as Map<*, *>)["siriAction"] })
        fixture.session.setSiriPressed(false)
        fixture.session.invokeSiri()
        val commands = fixture.commands()
        assertEquals(listOf(3L,2L,3L), commands.map { (it["params"] as Map<*, *>)["siriAction"] })
        commands.forEach { assertEquals("requestSiri", it["type"]) }
    }

    /** Capture and decrypt the real event encoder, using generated test keys only. */
    private class Fixture : AutoCloseable {
        private val output = ByteArrayOutputStream()
        private val testKey = ByteArray(32) { it.toByte() }
        private val reader = ControlCipher(testKey, testKey)
        val session = AirPlaySession(
            socket = Socket(),
            config = AirPlayConfig(deviceName = "test", deviceId = "02:00:00:00:00:02",
                btMac = "02:00:00:00:00:01", sourceVersion = "1.0",
                main = AirPlayDisplayConfig(widthPixels = 800, heightPixels = 480)),
            identity = AirPlayIdentity.generate(), pairings = PairingStore(), mfi = null,
            listener = object : AirPlaySessionListener {}, media = object : AirPlayMediaHandler {},
        )
        init {
            // A fake event socket avoids authentication/hardware while exercising production framing.
            setField("eventSocket", object : Socket() { override fun getOutputStream() = output })
            setField("eventCipher", ControlCipher(testKey, testKey))
        }
        private fun setField(name: String, value: Any) {
            AirPlaySession::class.java.getDeclaredField(name).apply { isAccessible = true }.set(session, value)
        }
        fun commands(): List<Map<*, *>> {
            val bytes = reader.decrypt(output.toByteArray()).data; output.reset()
            val messages = RtspMessage.parseMessages(bytes)
            assertEquals(0, messages.rest.size)
            return messages.messages.map { BplistCodec.decode(it.body) as Map<*, *> }
        }
        override fun close() = session.close()
    }
}
