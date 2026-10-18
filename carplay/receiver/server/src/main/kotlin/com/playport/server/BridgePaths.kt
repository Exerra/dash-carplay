package com.playport.server

import java.nio.file.Files
import java.nio.file.Path

/** Explicit helper paths never silently fall back to a different platform/adapter. */
object BridgePaths {
    fun resolve(configured: Path?, osName: String = System.getProperty("os.name"), root: Path = Path.of("")): Path? {
        val candidates = if (configured != null) listOf(configured) else when {
            osName.lowercase().contains("linux") -> listOf(root.resolve("linux/bt-bridge"))
            osName.lowercase().contains("mac") -> listOf(
                root.resolve("macos/bt-bridge/bt-bridge"),
                root.resolve("macos/bt-bridge/.build/bt-bridge"),
            )
            else -> emptyList()
        }
        return candidates.firstOrNull { Files.isRegularFile(it) && Files.isExecutable(it) }?.toAbsolutePath()
    }
}
