package com.playport.server

import java.io.ByteArrayOutputStream
import java.io.IOException
import java.nio.file.Path
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.junit.Assert.*
import org.junit.Test

class ProcessByteStreamTest {
    private fun child(mode: String): Process = ProcessBuilder(
        Path.of(System.getProperty("java.home"), "bin", "java").toString(), "-cp",
        Path.of(ByteStreamChild::class.java.protectionDomain.codeSource.location.toURI()).toString(),
        ByteStreamChild::class.java.name, mode,
    ).start()

    @Test(timeout = 10000)
    fun fragmentedReadsRetainByteOrderAndRepeatedEof() {
        val process = child("pattern")
        ProcessByteStream(process, "test").use { stream ->
            // Let several OS pipe chunks queue before requesting small fragments.
            Thread.sleep(150)
            val bytes = ByteArrayOutputStream()
            while (true) {
                val data = stream.recv(37, 2000) ?: error("unexpected timeout")
                if (data.isEmpty()) break
                assertTrue(data.size <= 37)
                bytes.write(data)
            }
            assertArrayEquals(ByteArray(65536) { it.toByte() }, bytes.toByteArray())
            assertArrayEquals(ByteArray(0), stream.recv(5, Long.MAX_VALUE))
        }
    }

    @Test(timeout = 10000)
    fun duplexEchoTimeoutAndClose() {
        val process = child("echo")
        val stream = ProcessByteStream(process, "test")
        try {
            assertNull(stream.recv(16, 10))
            val expected = ByteArray(4096) { (255 - it).toByte() }
            stream.send(expected)
            val actual = ByteArrayOutputStream()
            while (actual.size() < expected.size) actual.write(stream.recv(19, 2000) ?: error("echo timed out"))
            assertArrayEquals(expected, actual.toByteArray())
        } finally { stream.close() }
        assertArrayEquals(ByteArray(0), stream.recv(1, 0))
        assertThrows(IOException::class.java) { stream.send(byteArrayOf(1)) }
        assertFalse(process.isAlive)
    }

    @Test(timeout = 10000)
    fun closeReleasesAnIndefinitelyWaitingReader() {
        val process = child("wait")
        val stream = ProcessByteStream(process, "test")
        val executor = Executors.newSingleThreadExecutor()
        try {
            val pending = executor.submit<ByteArray?> { stream.recv(16, Long.MAX_VALUE) }
            stream.close()
            assertArrayEquals(ByteArray(0), pending.get(2, TimeUnit.SECONDS))
        } finally { stream.close(); executor.shutdownNow() }
    }
}
