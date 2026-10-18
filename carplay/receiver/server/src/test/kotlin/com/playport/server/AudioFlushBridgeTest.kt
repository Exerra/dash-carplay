package com.playport.server

import com.shilapi.xcertplay.airplay.AudioCodecKind
import com.shilapi.xcertplay.airplay.AudioFormat
import io.ktor.websocket.Frame
import java.nio.file.Path
import kotlinx.coroutines.channels.Channel
import org.junit.Assert.*
import org.junit.Test

class AudioFlushBridgeTest {
    @Test fun flushStopsQueuedSoundThenRestoresTheFormatForExistingClients() {
        val hub = WebHub(ServerConfig(identityDir=Path.of("."),stateDir=Path.of(".")),DisplayState(800,480,30,100))
        val frames = Channel<Frame>(16)
        val constructor = Class.forName("com.playport.server.WsClient").getDeclaredConstructor(Channel::class.java).apply { isAccessible=true }
        val field = hub.javaClass.getDeclaredField("clients").apply { isAccessible=true }
        @Suppress("UNCHECKED_CAST")
        val clients = field.get(hub) as MutableSet<Any>
        clients.add(constructor.newInstance(frames))
        val bridge = WebBridge(hub)
        val format = AudioFormat(AudioCodecKind.AAC_LC,48000,2,96)
        fun receive() = (frames.tryReceive().getOrThrow() as Frame.Binary).data
        bridge.onAudioStarted(102,format,0)
        val config = receive()
        bridge.onAudioFlushed(102)
        assertArrayEquals(Wire.audioStopped(102),receive())
        assertArrayEquals(config,receive())
        bridge.onAudioRtp(102,format,ByteArray(12)+byteArrayOf(1,2),1024)
        assertEquals(Wire.AUDIO_PACKET,receive()[0])
        bridge.onAudioFlushed(103); assertTrue(frames.tryReceive().isFailure)
        bridge.onAudioStopped(102); receive()
        bridge.onAudioFlushed(102); assertTrue(frames.tryReceive().isFailure)
        frames.close()
    }
}
