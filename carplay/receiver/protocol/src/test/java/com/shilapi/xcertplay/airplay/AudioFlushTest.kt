package com.shilapi.xcertplay.airplay

import java.io.Closeable
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.Socket
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import org.junit.Assert.*
import org.junit.Test

class AudioFlushTest {
    @Test fun rtspFlushCallsMediaWithoutTearingDownTheSession() {
        var flushes = 0
        val session = session(object : AirPlayMediaHandler {
            override fun onFlush(session: AirPlaySession, point: AudioFlushPoint) {
                assertEquals(AudioFlushPoint(65535, 4294967295L), point); flushes++
            }
        })
        try {
            session.activeStreams.addAll(listOf(102, 110))
            val handle = AirPlaySession::class.java.getDeclaredMethod("handle", RtspMessage.Request::class.java).apply { isAccessible=true }
            val response = handle.invoke(session, RtspMessage.Request("FLUSH", "/stream", "RTSP/1.0", mapOf("rtp-info" to "seq=65535;rtptime=4294967295"), byteArrayOf())) as RtspMessage.Response
            assertEquals(200, response.status); assertEquals(1, flushes)
            assertEquals(setOf(102,110), session.activeStreams)
        } finally { session.close() }
    }

    @Test fun engineFlushOnlyResetsAudioInTheRequestingSession() {
        val events = mutableListOf<Int>()
        val engine = CarPlayMediaEngine(object : MediaSink {
            override fun onAudioFlushed(type: Int) { events.add(type) }
        })
        val one = session(); val two = session()
        val audio = AudioStream(ByteArray(32), 102)
        val other = AudioStream(ByteArray(32), 103)
        var screenClosed = false
        val field = engine.javaClass.getDeclaredField("streams").apply { isAccessible=true }
        @Suppress("UNCHECKED_CAST")
        val streams = field.get(engine) as MutableMap<CarPlayMediaEngine.StreamKey, Closeable>
        streams[CarPlayMediaEngine.StreamKey(one,102)] = audio
        streams[CarPlayMediaEngine.StreamKey(two,103)] = other
        streams[CarPlayMediaEngine.StreamKey(one,110)] = Closeable { screenClosed=true }
        try {
            engine.onFlush(one, AudioFlushPoint(12, 1234))
            assertEquals(listOf(102), events); assertEquals(3, streams.size); assertFalse(screenClosed)
            var alive = false; audio.flush { alive=true }; assertTrue(alive)
        } finally { streams.values.forEach(Closeable::close); one.close(); two.close() }
    }

    @Test fun flushIsOrderedAfterInFlightDeliveryAndKeepsTheUdpStreamUsable() {
        val key = ByteArray(32)
        val stream = AudioStream(key,102)
        val entered = CountDownLatch(1); val release = CountDownLatch(1)
        val flushed = CountDownLatch(1); val second = CountDownLatch(1)
        val events = CopyOnWriteArrayList<String>(); val starts = AtomicInteger()
        val (port, _) = stream.listen(object : AudioStream.Listener {
            override fun onStarted(firstSample: Int) { starts.incrementAndGet() }
            override fun onRtp(rtp: ByteArray, sample: Int) {
                if (sample==0) {
                    events.add("first-start"); entered.countDown(); release.await(3,TimeUnit.SECONDS); events.add("first-end")
                } else { events.add("second"); second.countDown() }
            }
        })
        DatagramSocket().use { sender ->
            val counters = MicrophoneCounters()
            fun send() {
                val packet = MicrophonePacketizer.sealPacket(key,96,counters,byteArrayOf(1,2),1)
                sender.send(DatagramPacket(packet,packet.size,InetAddress.getByName("::1"),port))
            }
            try {
                send(); assertTrue(entered.await(3,TimeUnit.SECONDS))
                val thread = Thread { stream.flush { events.add("flush"); flushed.countDown() } }.apply { start() }
                assertFalse(flushed.await(100,TimeUnit.MILLISECONDS)); release.countDown()
                assertTrue(flushed.await(3,TimeUnit.SECONDS)); thread.join(3000)
                send(); assertTrue(second.await(3,TimeUnit.SECONDS))
                assertEquals(listOf("first-start","first-end","flush","second"), events.toList())
                assertEquals(1, starts.get()) // Keep transport/microphone negotiation alive.
            } finally { release.countDown(); stream.close() }
        }
        var resetAfterClose = false; stream.flush { resetAfterClose=true }; assertFalse(resetAfterClose)
    }


    @Test fun flushHeaderParsesOnlyUnsignedRtpBoundaries() {
        assertEquals(AudioFlushPoint(), AudioFlushPoint.parse(null))
        assertEquals(AudioFlushPoint(0, 0), AudioFlushPoint.parse("seq=0; rtptime=0"))
        assertEquals(AudioFlushPoint(), AudioFlushPoint.parse("seq=-1;rtptime=4294967296"))
        assertEquals(AudioFlushPoint(), AudioFlushPoint.parse("seq=65536;rtptime=99999999999999999999999"))
        assertEquals(AudioFlushPoint(), AudioFlushPoint.parse("seq=+2;rtptime=abc"))
        assertEquals(AudioFlushPoint(timestamp=123), AudioFlushPoint.parse("rtptime=123;ignored=other"))
    }

    @Test fun flushDropsStaleAuthenticatedAudioEvenAfterNewAudioArrives() {
        checkPackets(AudioFlushPoint(10, 1000), listOf(
            9 to 900, 10 to 1000, 8 to 950, 11 to 1960,
            12 to 145000, 13 to Int.MIN_VALUE,
        ), listOf(1000, 1960, 145000, Int.MIN_VALUE))
    }

    @Test fun timestampCutoffHandlesWrapAndExpiresOnTheNewTimeline() {
        checkPackets(AudioFlushPoint(timestamp=0xfffffff0L), listOf(
            1 to -32, 2 to -16, 3 to 0, 4 to -32,
            5 to 144000, 6 to Int.MIN_VALUE,
        ), listOf(-16, 0, 144000, Int.MIN_VALUE))
    }

    @Test fun sequenceOnlyCutoffHandlesWrapAndExpires() {
        checkPackets(AudioFlushPoint(sequence=65535), listOf(
            65534 to 1, 65535 to 2, 0 to 3, 65534 to 4,
            512 to 5, 32768 to 6,
        ), listOf(2, 3, 5, 6))
    }

    private fun checkPackets(point: AudioFlushPoint, packets: List<Pair<Int, Int>>, expected: List<Int>) {
        val key = ByteArray(32); val stream = AudioStream(key, 102)
        val received = CopyOnWriteArrayList<Int>(); val done = CountDownLatch(1)
        val starts = AtomicInteger()
        val (port, _) = stream.listen(object : AudioStream.Listener {
            override fun onStarted(firstSample: Int) { starts.incrementAndGet() }
            override fun onRtp(rtp: ByteArray, sample: Int) {
                received.add(sample)
                if (sample == expected.last()) done.countDown()
            }
        })
        try {
            stream.flush(point) {}
            DatagramSocket().use { sender ->
                packets.forEachIndexed { index, (sequence, timestamp) ->
                    val counters = MicrophoneCounters(sequence, timestamp, index.toLong())
                    val packet = MicrophonePacketizer.sealPacket(key, 96, counters, byteArrayOf(1,2), 1)
                    sender.send(DatagramPacket(packet, packet.size, InetAddress.getByName("::1"), port))
                }
            }
            assertTrue(done.await(3, TimeUnit.SECONDS))
            assertEquals(expected, received.toList()); assertEquals(1, starts.get())
        } finally { stream.close() }
    }

    @Test fun flushAlsoRejectsAPacketAlreadyDecryptedBeforeTheCutoff() {
        val key = ByteArray(32); val stream = AudioStream(key, 102)
        val decrypted = CountDownLatch(1); val release = CountDownLatch(1); val done = CountDownLatch(1)
        val received = CopyOnWriteArrayList<Int>()
        val (port, _) = stream.listen(object : AudioStream.Listener {
            override fun onPacket(wire: ByteArray, rtp: ByteArray?, sample: Int?, error: Throwable?) {
                if (sample == 5) { decrypted.countDown(); release.await(3, TimeUnit.SECONDS) }
            }
            override fun onRtp(rtp: ByteArray, sample: Int) { received.add(sample); done.countDown() }
        })
        try {
            DatagramSocket().use { sender ->
                fun send(timestamp: Int) {
                    val packet = MicrophonePacketizer.sealPacket(key, 96,
                        MicrophoneCounters(timestamp, timestamp, timestamp.toLong()), byteArrayOf(1,2), 1)
                    sender.send(DatagramPacket(packet, packet.size, InetAddress.getByName("::1"), port))
                }
                send(5); assertTrue(decrypted.await(3, TimeUnit.SECONDS))
                stream.flush(AudioFlushPoint(timestamp=100)) {}; release.countDown()
                send(200); assertTrue(done.await(3, TimeUnit.SECONDS)); assertEquals(listOf(200), received.toList())
            }
        } finally { release.countDown(); stream.close() }
    }

    private fun session(media: AirPlayMediaHandler = object : AirPlayMediaHandler {}) = AirPlaySession(
        Socket(), AirPlayConfig("test","02:00:00:00:00:02","02:00:00:00:00:01","1.0",
            main=AirPlayDisplayConfig(widthPixels=800,heightPixels=480)),
        AirPlayIdentity.generate(), PairingStore(), null, object : AirPlaySessionListener {}, media,
    )
}
