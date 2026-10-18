package com.playport.server

import com.shilapi.xcertplay.airplay.AudioCodecKind
import com.shilapi.xcertplay.airplay.MicrophoneConfig
import java.io.Closeable
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/** One clock/counter sequence for captured input, falling back to silence between captured frames. */
internal class MicrophoneFeed(config: MicrophoneConfig) : Closeable {
    private val lock = Any()
    private var closed = false
    private val captured = java.util.ArrayDeque<ByteArray>()
    private val opus = config.codec == AudioCodecKind.OPUS
    private val frameBytes = config.frameBytes
    val uplink: MicUplink
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
                if (!closed) uplink.send(captured.pollFirst() ?: silence, config.samplesPerPacket)
            }
        }, 0, config.frameMillis.toLong(), TimeUnit.MILLISECONDS)
    }

    fun offer(payload: ByteArray, isOpus: Boolean): Boolean = synchronized(lock) {
        if (closed || isOpus != opus || payload.isEmpty() || (opus && payload.size > 4096) || (!opus && payload.size != frameBytes)) return false
        while (captured.size >= 3) captured.removeFirst()
        captured.addLast(payload)
        true
    }

    override fun close() = synchronized(lock) {
        if (!closed) {
            closed = true
            executor.shutdownNow()
            uplink.close()
        }
    }
}
