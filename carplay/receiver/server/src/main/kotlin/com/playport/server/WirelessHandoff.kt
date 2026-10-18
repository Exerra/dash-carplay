package com.playport.server

/** Release this session's Bluetooth bootstrap only after its authenticated Wi-Fi tunnel is ready. */
internal class WirelessHandoff {
    private var owner: Any? = null
    private var release: (() -> Unit)? = null
    private var requested = false
    private var ready = false

    @Synchronized fun begin(session: Any, releaseBootstrap: () -> Unit) {
        owner = session
        release = releaseBootstrap
        requested = false
        ready = false
    }

    @Synchronized fun clear(session: Any) {
        if (owner === session) { owner = null; release = null }
    }

    @Synchronized fun request(session: Any) {
        if (owner !== session) return
        requested = true
        complete()
    }

    @Synchronized fun tunnelReady(session: Any) {
        if (owner !== session) return
        ready = true
        complete()
    }

    private fun complete() {
        if (!requested || !ready) return
        val action = release ?: return
        release = null
        action()
    }
}
