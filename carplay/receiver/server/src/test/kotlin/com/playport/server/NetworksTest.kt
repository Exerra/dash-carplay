package com.playport.server

import java.net.InetAddress
import java.net.Inet6Address
import org.junit.Assert.*
import org.junit.Test

class NetworksTest {
    @Test fun handoffAndDiscoveryKeepBothFamiliesOnTheSelectedInterface() {
        val v4 = InetAddress.getByName("10.42.0.1")
        val v6 = InetAddress.getByName("fe80::1234")
        val addresses = Networks.interfaceAddresses(v4, listOf(v4, v6, InetAddress.getByName("2001:db8::1")), 7)
        assertEquals(2, addresses.size)
        assertEquals(v4, addresses[0])
        assertEquals(7, (addresses[1] as Inet6Address).scopeId)
        assertEquals(listOf(addresses[1], v4), Networks.interfaceAddresses(addresses[1], listOf(v4, v6), 7))
    }

    @Test fun doesNotBorrowAnUnusableOrUnrelatedAddressFamily() {
        val v6 = InetAddress.getByName("fe80::1234")
        assertEquals(listOf(v6), Networks.interfaceAddresses(v6,
            listOf(InetAddress.getByName("127.0.0.1"), InetAddress.getByName("169.254.1.1")), 7))
    }
}
