package com.playport.server

import com.shilapi.xcertplay.airplay.AudioCodecKind
import com.shilapi.xcertplay.airplay.MicrophoneConfig
import java.io.Closeable
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/** A timed, encrypted silence input. It opens no capture device and enables no voice features. */
internal class SilentMicrophone(config: MicrophoneConfig) : Closeable {
    private val lock = Any()
    private var closed = false
    private val uplink: MicUplink
    private val executor = Executors.newSingleThreadScheduledExecutor { task ->
        Thread(task, "carplay-silent-input").apply { isDaemon = true }
    }

    init {
        require(config.codec == AudioCodecKind.LPCM || config.codec == AudioCodecKind.OPUS)
        require(config.codec != AudioCodecKind.OPUS || (config.channels == 1 && config.frameMillis == 20))
        require(config.frameBytes in 1..65536 && config.frameMillis in 5..60)
        uplink = MicUplink(config)
        // A mono, 20 ms Opus silence frame decodes to 960 zero samples at the
        // 48 kHz RTP clock. PCM silence is zero in either byte order.
        val silence = if (config.codec == AudioCodecKind.OPUS) byteArrayOf(0xf8.toByte(), 0xff.toByte(), 0xfe.toByte())
            else ByteArray(config.frameBytes)
        executor.scheduleWithFixedDelay({
            synchronized(lock) {
                if (!closed) uplink.send(silence, config.samplesPerPacket)
            }
        }, 0, config.frameMillis.toLong(), TimeUnit.MILLISECONDS)
    }

    override fun close() = synchronized(lock) {
        if (!closed) {
            closed = true
            executor.shutdownNow()
            uplink.close()
        }
    }
}
