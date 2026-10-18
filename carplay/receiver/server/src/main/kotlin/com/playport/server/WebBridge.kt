package com.playport.server

import com.shilapi.xcertplay.airplay.AudioFormat
import com.shilapi.xcertplay.airplay.MediaSink
import com.shilapi.xcertplay.airplay.MicrophoneConfig
import com.shilapi.xcertplay.airplay.VideoCodec
import java.util.concurrent.atomic.AtomicBoolean

/** Forwards decrypted CarPlay media to the browser hub without decoding it server-side. */
class WebBridge(private val hub: WebHub, private val silentMicrophone: Boolean = false) : MediaSink {
    private val recoveryHandlers = java.util.concurrent.ConcurrentHashMap<Int, () -> Unit>()
    private val diagnosticHandlers = java.util.concurrent.ConcurrentHashMap<Int, (String) -> Unit>()
    private val firstFrameReported = AtomicBoolean(false)

    override fun onVideoCodec(type: Int, codec: VideoCodec) {
        hub.setVideoCodec(type, codec)
    }

    override fun onVideoConfig(type: Int, codecData: ByteArray) {
        hub.broadcastVideoConfig(type, codecData)
    }

    override fun onVideoFrame(type: Int, naluBytes: ByteArray) {
        val codec = hub.videoCodec(type) ?: VideoCodec.H264
        hub.broadcastVideoFrame(type, Wire.isKeyFrame(naluBytes, codec), naluBytes)
        if (firstFrameReported.compareAndSet(false, true)) {
            diagnosticHandlers[type]?.invoke("first frame rendered")
        }
    }

    override fun setVideoRecoveryHandler(type: Int, handler: () -> Unit) {
        recoveryHandlers[type] = handler
    }

    override fun setVideoDiagnosticHandler(type: Int, handler: (String) -> Unit) {
        diagnosticHandlers[type] = handler
    }

    override fun onScreenStreamActive(type: Int, active: Boolean) {
        hub.broadcastScreenActive(type, active)
    }

    override fun onAudioStarted(type: Int, format: AudioFormat, firstSample: Int) {
        hub.broadcastAudioConfig(type, format)
    }

    override fun onAudioRtp(type: Int, format: AudioFormat, rtp: ByteArray, sample: Int) {
        if (rtp.size <= RTP_HEADER_BYTES) return
        val timestampUs = (sample.toLong() and 0xffff_ffffL) * 1_000_000L / format.sampleRate
        val payload = when (format.codec) {
            com.shilapi.xcertplay.airplay.AudioCodecKind.LPCM -> {
                val tail = rtp.copyOfRange(RTP_HEADER_BYTES, rtp.size)
                Wire.byteSwapS16(tail)
            }
            else -> rtp.copyOfRange(RTP_HEADER_BYTES, rtp.size)
        }
        if (payload.isEmpty()) return
        hub.broadcastAudioPacket(type, timestampUs, payload)
    }

    override fun onAudioStopped(type: Int) {
        hub.broadcastAudioStopped(type)
    }

    override fun onAudioFlushed(type: Int) {
        hub.broadcastAudioFlushed(type)
    }

    @Volatile private var microphone: MicrophoneFeed? = null

    @Synchronized override fun onMicrophoneStarted(type: Int, config: MicrophoneConfig) {
        endMicrophone()
        val feed = MicrophoneFeed(config)
        microphone = feed
        if (!silentMicrophone) hub.microphoneRequested(feed.uplink)
    }

    override fun onMicrophoneStopped(type: Int) { endMicrophone() }

    @Synchronized fun endMicrophone() {
        val current = microphone
        microphone = null
        current?.close()
        if (current != null && !silentMicrophone) hub.microphoneStopped()
    }

    fun sendMicrophonePayload(payload: ByteArray, opus: Boolean): Boolean =
        !silentMicrophone && (microphone?.offer(payload, opus) ?: false)

    /** Asks the phone for a keyframe; called when a browser joins or reports a decode stall. */
    fun requestKeyFrame(streamType: Int) {
        recoveryHandlers[streamType]?.invoke()
    }

    val hasRecoveryHandler: Boolean get() = recoveryHandlers.isNotEmpty()

    private companion object {
        const val RTP_HEADER_BYTES = 12
    }
}
