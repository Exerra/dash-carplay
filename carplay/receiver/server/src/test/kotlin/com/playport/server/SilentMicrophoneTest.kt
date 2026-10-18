package com.playport.server

import com.shilapi.xcertplay.airplay.AirPlayCrypto
import com.shilapi.xcertplay.airplay.AudioCodecKind
import com.shilapi.xcertplay.airplay.MicrophoneConfig
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.SocketTimeoutException
import org.junit.Assert.*
import org.junit.Test

class SilentMicrophoneTest {
    @Test fun sendsTimedEncryptedPcmSilenceAndStopsAtClose() = verifyPackets(AudioCodecKind.LPCM)
    @Test fun sendsTimedEncryptedOpusSilenceAndStopsAtClose() = verifyPackets(AudioCodecKind.OPUS)

    private fun verifyPackets(codec: AudioCodecKind) {
        val host = InetAddress.getByName("127.0.0.1")
        DatagramSocket(0, host).use { receiver ->
            receiver.soTimeout = 2000
            val key = ByteArray(32) { it.toByte() }
            val config = MicrophoneConfig("telephony", 16000, 1, 100, 20, host, receiver.localPort, key, codec)
            fun receive(): ByteArray {
                val packet = DatagramPacket(ByteArray(2048), 2048)
                receiver.receive(packet)
                return packet.data.copyOf(packet.length)
            }
            val first: ByteArray
            val second: ByteArray
            SilentMicrophone(config).use {
                first = receive()
                second = receive()
            }
            for (packet in listOf(first, second)) {
                val nonce = ByteArray(12)
                packet.copyInto(nonce, 4, packet.size - 8)
                val audio = AirPlayCrypto.chachaOpen(key, nonce,
                    packet.copyOfRange(12, packet.size - 8), packet.copyOfRange(4, 12))
                if (codec == AudioCodecKind.LPCM) {
                    assertEquals(config.frameBytes, audio.size)
                    assertTrue(audio.all { it == 0.toByte() })
                } else assertArrayEquals(byteArrayOf(0xf8.toByte(), 0xff.toByte(), 0xfe.toByte()), audio)
            }
            fun timestamp(packet: ByteArray): Int =
                (4..7).fold(0) { value, index -> (value shl 8) or (packet[index].toInt() and 255) }
            assertEquals(config.samplesPerPacket, timestamp(second) - timestamp(first))
            assertEquals(1, second[3].toInt() and 255)
            // Drain any packet already in flight at close; no sender may survive teardown.
            receiver.soTimeout = 100
            repeat(10) {
                try { receive() } catch (_: SocketTimeoutException) { return }
            }
            fail("Silent input continued after close")
        }
    }
}
