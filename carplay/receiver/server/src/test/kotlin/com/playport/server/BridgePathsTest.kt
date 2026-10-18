package com.playport.server

import java.nio.file.Files
import java.nio.file.attribute.PosixFilePermissions
import org.junit.Assert.*
import org.junit.Test

class BridgePathsTest {
    @Test
    fun choosesOnlyTheHostPlatformAndHonoursExplicitFailure() {
        val root = Files.createTempDirectory("bridge-paths")
        val linux = root.resolve("linux/bt-bridge")
        val mac = root.resolve("macos/bt-bridge/bt-bridge")
        try {
            for (file in listOf(linux, mac)) {
                Files.createDirectories(file.parent)
                Files.writeString(file, "#!/bin/sh\nexit 0\n")
                Files.setPosixFilePermissions(file, PosixFilePermissions.fromString("rwx------"))
            }
            assertEquals(linux, BridgePaths.resolve(null, "Linux", root))
            assertEquals(mac, BridgePaths.resolve(null, "Mac OS X", root))
            assertNull(BridgePaths.resolve(root.resolve("missing"), "Linux", root))
            assertNull(BridgePaths.resolve(null, "Windows", root))
        } finally { root.toFile().deleteRecursively() }
    }
}
