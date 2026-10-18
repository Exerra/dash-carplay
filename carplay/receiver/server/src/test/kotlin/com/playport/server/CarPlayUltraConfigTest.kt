package com.playport.server

import java.nio.file.Path
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class CarPlayUltraConfigTest {
    @Test fun ultraRequiresExplicitOptIn() {
        val defaults = ServerConfig(identityDir = Path.of("."), stateDir = Path.of("."), carPlayUltra = false)
        assertFalse(ServerConfig.fromArgs(arrayOf("--wireless"), defaults).carPlayUltra)
        assertTrue(ServerConfig.fromArgs(arrayOf("--wireless", "--carplay-ultra"), defaults).carPlayUltra)
    }
}
