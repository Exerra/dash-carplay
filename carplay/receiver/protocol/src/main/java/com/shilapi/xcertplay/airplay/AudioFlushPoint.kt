package com.shilapi.xcertplay.airplay

/** First retained RTP packet, as supplied by the phone in FLUSH's RTP-Info header. */
data class AudioFlushPoint(val sequence: Int? = null, val timestamp: Long? = null) {
    companion object {
        fun parse(header: String?): AudioFlushPoint {
            val fields = header.orEmpty().split(';').mapNotNull { field ->
                val parts = field.trim().split('=', limit = 2)
                if (parts.size == 2) parts[0].lowercase() to parts[1].trim() else null
            }.toMap()
            fun unsigned(name: String, max: Long): Long? = fields[name]
                ?.takeIf { it.isNotEmpty() && it.all { c -> c in '0'..'9' } }
                ?.toLongOrNull()?.takeIf { it in 0..max }
            return AudioFlushPoint(unsigned("seq", 0xffff)?.toInt(), unsigned("rtptime", 0xffffffffL))
        }
    }
}
