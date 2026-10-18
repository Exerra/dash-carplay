package com.playport.server

import org.junit.Assert.assertEquals
import org.junit.Test

class WirelessHandoffTest {
    @Test fun waitsForTunnelAndReleasesOnlyOnce() {
        val handoff = WirelessHandoff()
        val session = Any()
        var releases = 0
        handoff.begin(session) { releases++ }
        handoff.request(session)
        assertEquals(0, releases)
        handoff.tunnelReady(session)
        handoff.request(session)
        handoff.tunnelReady(session)
        assertEquals(1, releases)
    }

    @Test fun alsoHandlesTunnelBeforeRequest() {
        val handoff = WirelessHandoff()
        val session = Any()
        var releases = 0
        handoff.begin(session) { releases++ }
        handoff.tunnelReady(session)
        assertEquals(0, releases)
        handoff.request(session)
        assertEquals(1, releases)
    }

    @Test fun staleAndEndedSessionsCannotReleaseAnotherBootstrap() {
        val handoff = WirelessHandoff()
        val old = Any()
        val current = Any()
        var releases = 0
        handoff.begin(old) { releases += 100 }
        handoff.request(old)
        handoff.begin(current) { releases++ }
        handoff.tunnelReady(old)
        handoff.request(old)
        handoff.clear(old)
        assertEquals(0, releases)
        handoff.tunnelReady(current)
        handoff.clear(current)
        handoff.request(current)
        assertEquals(0, releases)
    }
}
