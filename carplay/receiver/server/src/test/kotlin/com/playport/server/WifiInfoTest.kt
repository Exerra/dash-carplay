package com.playport.server

import org.junit.Assert.*
import org.junit.Test

class WifiInfoTest {
    @Test
    fun keepsSsidAndChannelTogetherAcrossAdaptersAndSpaces() {
        val networks = WifiInfo.parseLinuxNetworks("""
            phy#1
                Interface wlan1
                    ssid Guest Network
                    channel 6 (2437 MHz), width: 20 MHz
            phy#0
                Interface wlan0
                    ssid Mājas Wi-Fi
                    channel 36 (5180 MHz), width: 80 MHz
        """.trimIndent())
        assertEquals(listOf(WifiNetwork("Guest Network", 6), WifiNetwork("Mājas Wi-Fi", 36)), networks)
    }

    @Test
    fun ignoresDisconnectedInterfacesRatherThanCombiningPartialData() {
        assertTrue(WifiInfo.parseLinuxNetworks("""
            Interface wlan0
                ssid Home
            Interface wlan1
                channel 11 (2462 MHz)
        """.trimIndent()).isEmpty())
    }

    @Test fun usesOnlyAnAccessPointsOwnAddressForTheBssidHint() {
        val networks = WifiInfo.parseLinuxNetworks("""
            Interface wlan0
                addr d8:3a:dd:c1:a5:73
                ssid OpenDash
                type AP
                channel 36 (5180 MHz)
            Interface wlan1
                addr 00:11:22:33:44:55
                ssid Home
                type managed
                channel 6 (2437 MHz)
        """.trimIndent())
        assertEquals("d8:3a:dd:c1:a5:73", networks[0].accessPointBssid)
        assertNull(networks[1].accessPointBssid)
    }
}
