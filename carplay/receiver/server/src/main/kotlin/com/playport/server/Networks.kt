package com.playport.server

import java.net.Inet4Address
import java.net.Inet6Address
import java.net.InetAddress
import java.net.NetworkInterface
import java.net.SocketException

/** LAN address discovery for mDNS advertisement and the AirPlay RTSP bind. */
object Networks {
    /** Keep IPv4 and link-local IPv6 on the exact interface selected for CarPlay. */
    fun interfaceAddresses(primary: InetAddress): List<InetAddress> {
        val network = NetworkInterface.getByInetAddress(primary) ?: return listOf(primary)
        return interfaceAddresses(primary, network.inetAddresses.toList(), network.index)
    }

    internal fun interfaceAddresses(primary: InetAddress, candidates: List<InetAddress>, index: Int): List<InetAddress> {
        val alternate = if (primary is Inet4Address) {
            candidates.filterIsInstance<Inet6Address>().firstOrNull { it.isLinkLocalAddress }?.let {
                Inet6Address.getByAddress(null, it.address, index)
            }
        } else {
            candidates.filterIsInstance<Inet4Address>().firstOrNull { !it.isLoopbackAddress && !it.isLinkLocalAddress }
        }
        return listOfNotNull(primary, alternate).distinct()
    }

    fun detectLanAddress(): InetAddress {
        val candidates = mutableListOf<InetAddress>()
        try {
            NetworkInterface.getNetworkInterfaces().toList().forEach { iface ->
                if (!iface.isUp || iface.isLoopback || iface.isPointToPoint) return@forEach
                val name = iface.name.lowercase()
                if (name.startsWith("utun") || name.startsWith("awdl") || name.startsWith("llw") ||
                    name.startsWith("bridge") || name.startsWith("ap1")
                ) {
                    return@forEach
                }
                iface.inetAddresses.toList().forEach { address ->
                    if (address is Inet4Address && !address.isLoopbackAddress && !address.isLinkLocalAddress) {
                        candidates.add(address)
                    }
                }
            }
        } catch (error: SocketException) {
            throw IllegalStateException("No usable network interface: ${error.message}", error)
        }
        return candidates.firstOrNull() ?: InetAddress.getLoopbackAddress()
    }

    fun resolve(bindAddress: String?): InetAddress =
        if (bindAddress.isNullOrBlank()) detectLanAddress() else InetAddress.getByName(bindAddress)
}
