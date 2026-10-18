package com.playport.server

import java.net.InetAddress
import java.net.Inet6Address
import java.net.ServerSocket
import java.net.Socket
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.junit.Assert.*
import org.junit.Test

class CarPlayControlDiscoveryTest {
    @Test fun linkLocalControlEndpointUsesTheReceiversInterfaceScope() {
        val bytes = InetAddress.getByName("fe80::1234").address
        val local = Inet6Address.getByAddress(null, bytes, 7)
        val peer = Inet6Address.getByAddress(null, bytes, 0)
        assertEquals(7, (CarPlayControlDiscovery.scopedPeer(peer, local) as Inet6Address).scopeId)
    }

    @Test fun selectsOnlyThePairedPhoneDespiteFormattingDifferences() {
        assertTrue(CarPlayControlDiscovery.matchesPhone("28:8F:F6:71:52:02", "28-8f-f6-71-52-02"))
        assertFalse(CarPlayControlDiscovery.matchesPhone("28:8F:F6:71:52:02", "00:00:00:00:00:01"))
        assertFalse(CarPlayControlDiscovery.matchesPhone("invalid", "invalid"))
        assertFalse(CarPlayControlDiscovery.matchesPhone("28:8F:F6:71:52:02", null))
    }

    @Test fun actualTcpProbeUsesTheReceiverInterfaceAndIdentity() {
        val local = InetAddress.getByName("127.0.0.1")
        val worker = Executors.newSingleThreadExecutor()
        try {
            ServerSocket(0, 1, local).use { server ->
                server.soTimeout = 4000
                val request = worker.submit<String> {
                    server.accept().use { peer ->
                        peer.soTimeout = 4000
                        val reader = peer.getInputStream().bufferedReader(Charsets.US_ASCII)
                        val lines = mutableListOf<String>()
                        while (true) {
                            val line = reader.readLine() ?: break
                            if (line.isEmpty()) break
                            lines.add(line)
                        }
                        peer.getOutputStream().write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n".toByteArray())
                        lines.joinToString("\n")
                    }
                }
                val status = Socket().use {
                    CarPlayControlDiscovery.connectProbe(it, local, local, server.localPort, "366.0", "02:11:22:33:44:55")
                }
                assertEquals("HTTP/1.1 200 OK", status)
                val text = request.get(4, TimeUnit.SECONDS)
                assertTrue(text.startsWith("GET /ctrl-int/1/connect HTTP/1.1\n"))
                assertTrue(text.contains("AirPlay-Receiver-Device-ID: 021122334455"))
            }
        } finally { worker.shutdownNow() }
    }
}
