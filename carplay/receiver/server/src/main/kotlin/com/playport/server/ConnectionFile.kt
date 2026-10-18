package com.playport.server

import java.io.Closeable
import java.net.URLEncoder
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardCopyOption
import java.nio.file.attribute.PosixFilePermissions
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.put

/** Owner-readable rendezvous file for Dash; no token needs to be stored in Dash settings. */
class ConnectionFile(private val config: ServerConfig) : Closeable {
    private val file: Path = config.stateDir.resolve("connection.json")
    private val contents = buildJsonObject {
        val scheme = if (config.insecureHttp) "ws" else "wss"
        val token = URLEncoder.encode(config.accessToken, Charsets.UTF_8)
        put("url", "$scheme://${config.webBindAddress}:${config.httpPort}/ws?token=$token")
        put("pid", ProcessHandle.current().pid())
    }.toString()

    fun write() {
        Files.createDirectories(config.stateDir)
        val temporary = Files.createTempFile(config.stateDir, ".connection-", ".tmp")
        try {
            Files.setPosixFilePermissions(temporary, PosixFilePermissions.fromString("rw-------"))
            Files.writeString(temporary, contents)
            Files.move(temporary, file, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE)
        } finally {
            Files.deleteIfExists(temporary)
        }
    }

    override fun close() {
        if (Files.exists(file) && Files.readString(file) == contents) Files.deleteIfExists(file)
    }
}
