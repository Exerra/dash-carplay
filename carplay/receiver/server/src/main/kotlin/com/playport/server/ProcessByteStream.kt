package com.playport.server

import com.shilapi.xcertplay.transport.BlockingDuplexByteStream
import java.io.Closeable
import java.io.InputStream
import java.io.OutputStream
import java.io.IOException
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

/**
 * A [BlockingDuplexByteStream] over a child process' standard input/output.
 *
 * Used for the macOS Bluetooth bridge helper: the helper owns the RFCOMM channel and pipes its
 * bytes through stdin/stdout while this side speaks the iAP2 link.
 */
class ProcessByteStream(
    private val process: Process,
    private val label: String,
) : BlockingDuplexByteStream, Closeable {
    private val inbound = LinkedBlockingQueue<ByteArray>(128)
    private val input: InputStream = process.inputStream
    private val output: OutputStream = process.outputStream
    private val writeLock = Any()
    private val readLock = Any()
    private var pending = ByteArray(0)
    private var offset = 0
    @Volatile private var ended = false
    @Volatile private var closed = false

    init {
        Thread({ pump() }, "$label-reader").apply {
            isDaemon = true
            start()
        }
        Thread({ drainErrors() }, "$label-stderr").apply {
            isDaemon = true
            start()
        }
    }

    private fun pump() {
        val buffer = ByteArray(8192)
        try {
            while (!closed) {
                val count = input.read(buffer)
                if (count < 0) break
                if (count > 0) {
                    val chunk = buffer.copyOf(count)
                    while (!closed && !inbound.offer(chunk, 100, TimeUnit.MILLISECONDS)) { }
                }
            }
        } catch (_: Exception) {
            // Falls through to the end marker.
        } finally {
            ended = true
            inbound.offer(ByteArray(0))
        }
    }

    private fun drainErrors() {
        val logger = org.slf4j.LoggerFactory.getLogger("bt-bridge")
        try {
            process.errorStream.bufferedReader().forEachLine { line ->
                if (line.isNotBlank()) logger.info("$label: $line")
            }
        } catch (_: Exception) {
        }
    }

    override fun send(data: ByteArray) {
        synchronized(writeLock) {
            if (closed) throw IOException("$label is closed")
            output.write(data)
            output.flush()
        }
    }

    override fun recv(maxBytes: Int, timeoutMillis: Long): ByteArray? = synchronized(readLock) {
        require(maxBytes > 0 && timeoutMillis >= 0)
        if (closed) return@synchronized ByteArray(0)
        if (offset == pending.size) {
            val queued = inbound.poll()
            if (queued == null && ended) return@synchronized ByteArray(0)
            pending = queued ?: if (timeoutMillis == Long.MAX_VALUE) inbound.take()
                else inbound.poll(timeoutMillis, TimeUnit.MILLISECONDS) ?: return@synchronized null
            offset = 0
        }
        val end = minOf(pending.size, offset + minOf(maxBytes, pending.size - offset))
        pending.copyOfRange(offset, end).also { offset = end }
    }

    override fun close() {
        closed = true
        ended = true
        inbound.clear()
        inbound.offer(ByteArray(0))
        // Terminate before closing the pipes so a blocked producer/write is released.
        process.destroy()
        runCatching { output.close() }
        runCatching { input.close() }
        if (!process.waitFor(2, TimeUnit.SECONDS)) process.destroyForcibly()
    }
}
