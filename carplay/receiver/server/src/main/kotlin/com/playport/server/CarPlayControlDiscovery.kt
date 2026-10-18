package com.playport.server

import com.shilapi.xcertplay.airplay.AirPlayConfig
import com.shilapi.xcertplay.network.CarPlayBonjourProtocol
import java.io.Closeable
import java.net.Inet4Address
import java.net.Inet6Address
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import javax.jmdns.JmDNS
import javax.jmdns.ServiceEvent
import javax.jmdns.ServiceListener
import org.slf4j.LoggerFactory

/** Invites the selected iPhone's Bonjour control endpoint after Bluetooth handoff. */
class CarPlayControlDiscovery(
    private val config: AirPlayConfig,
    private val localAddress: InetAddress,
    private val targetBluetoothAddress: () -> String?,
    private val sessionActive: () -> Boolean,
) : Closeable {
    private val log = LoggerFactory.getLogger("carplay-control")
    private data class Endpoint(val local: InetAddress, val address: InetAddress, val port: Int, val bluetoothId: String?)
    private val endpoints = ConcurrentHashMap<String, Endpoint>()
    private val attemptedAt = ConcurrentHashMap<String, Long>()
    private val worker = Executors.newSingleThreadScheduledExecutor { task ->
        Thread(task, "carplay-control").apply { isDaemon = true }
    }
    @Volatile private var closed = false
    @Volatile private var activeSocket: Socket? = null
    private val registries = mutableListOf<JmDNS>()
    private val type = "${CarPlayBonjourProtocol.CARPLAY_CONTROL_SERVICE_TYPE}.local."
    private val listener = object : ServiceListener {
        override fun serviceAdded(event: ServiceEvent) {
            if (!closed) event.dns.requestServiceInfo(event.type, event.name, true)
        }
        override fun serviceRemoved(event: ServiceEvent) {
            val key = "${event.name}|${event.dns.inetAddress is Inet4Address}"
            endpoints.remove(key)
            attemptedAt.remove(key)
        }
        override fun serviceResolved(event: ServiceEvent) {
            if (closed || event.info.port !in 1..65535) return
            val source = event.dns.inetAddress
            val address = event.info.inetAddresses.firstOrNull {
                (it is Inet4Address) == (source is Inet4Address)
            } ?: return
            val key = "${event.name}|${source is Inet4Address}"
            if (endpoints.size < 32 || endpoints.containsKey(key)) {
                endpoints[key] = Endpoint(source, scopedPeer(address, source), event.info.port, event.info.getPropertyString("id"))
                log.info("resolved iPhone control endpoint {}:{}", address.hostAddress, event.info.port)
            }
        }
    }

    fun start() {
        check(!closed && registries.isEmpty())
        try {
            for (address in Networks.interfaceAddresses(localAddress)) {
                val registry = JmDNS.create(address, "opendash-control-${config.deviceId.replace(":", "")}")
                registries.add(registry)
                registry.addServiceListener(type, listener)
            }
        } catch (error: Exception) {
            close()
            throw error
        }
        worker.scheduleWithFixedDelay(::invite, 1, 5, TimeUnit.SECONDS)
    }

    private fun invite() {
        if (closed || sessionActive()) return
        val target = targetBluetoothAddress() ?: return
        for ((name, endpoint) in endpoints) {
            if (closed || sessionActive()) return
            if (!matchesPhone(target, endpoint.bluetoothId)) continue
            val now = System.nanoTime()
            val previous = attemptedAt[name]
            if (previous != null && now - previous < TimeUnit.SECONDS.toNanos(15)) continue
            attemptedAt[name] = now
            try {
                val status = Socket().use { socket ->
                    activeSocket = socket
                    if (closed) return
                    connectProbe(socket, endpoint.local, endpoint.address, endpoint.port, config.sourceVersion, config.deviceId)
                }
                log.info("selected iPhone connect request: {}", status)
            } catch (error: Exception) {
                if (!closed) log.warn("selected iPhone connect request failed: {}", error.message)
            } finally { activeSocket = null }
        }
    }

    override fun close() {
        closed = true
        activeSocket?.runCatching { close() }
        worker.shutdownNow()
        registries.forEach { registry -> runCatching { registry.removeServiceListener(type, listener); registry.close() } }
        registries.clear()
        endpoints.clear()
    }

    companion object {
        internal fun scopedPeer(peer: InetAddress, local: InetAddress): InetAddress =
            if (peer is Inet6Address && peer.isLinkLocalAddress && local is Inet6Address && local.scopeId > 0) {
                Inet6Address.getByAddress(null, peer.address, local.scopeId)
            } else peer

        internal fun matchesPhone(target: String, advertised: String?): Boolean {
            fun normalize(value: String) = value.replace(":", "").replace("-", "").lowercase()
            val expected = normalize(target)
            return expected.matches(Regex("[0-9a-f]{12}")) && advertised != null && expected == normalize(advertised.trimEnd('\u0000'))
        }

        internal fun connectProbe(socket: Socket, local: InetAddress, phone: InetAddress, port: Int,
                                  sourceVersion: String, deviceId: String): String {
            socket.bind(InetSocketAddress(local, 0))
            socket.connect(InetSocketAddress(phone, port), 2500)
            socket.soTimeout = 2500
            socket.getOutputStream().apply {
                write(CarPlayBonjourProtocol.connectProbeRequest(phone.hostAddress, port, sourceVersion, deviceId).toByteArray(Charsets.US_ASCII))
                flush()
            }
            // Bound the response line rather than buffering arbitrary remote data.
            val line = StringBuilder()
            while (line.length < 1024) {
                val next = socket.getInputStream().read()
                check(next >= 0) { "iPhone control endpoint closed without a status" }
                if (next == 10) return line.toString().trimEnd('\r')
                line.append(next.toChar())
            }
            error("iPhone control status line too long")
        }
    }
}
