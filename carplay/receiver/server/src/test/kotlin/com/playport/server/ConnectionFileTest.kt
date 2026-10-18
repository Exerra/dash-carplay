package com.playport.server

import java.nio.file.Files
import java.nio.file.attribute.PosixFilePermission
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import org.junit.Assert.*
import org.junit.Test

class ConnectionFileTest {
    @Test
    fun rendezvousIsPrivateAndDoesNotDeleteASuccessor() {
        val directory = Files.createTempDirectory("carplay-connection")
        val file = directory.resolve("connection.json")
        val config = ServerConfig(identityDir = directory, stateDir = directory, insecureHttp = true, accessToken = "a&b")
        try {
            val first = ConnectionFile(config)
            first.write()
            val value = Json.parseToJsonElement(Files.readString(file)).jsonObject
            assertEquals("ws://127.0.0.1:8080/ws?token=a%26b", value["url"]!!.jsonPrimitive.content)
            assertEquals(setOf(PosixFilePermission.OWNER_READ, PosixFilePermission.OWNER_WRITE), Files.getPosixFilePermissions(file))
            val next = ConnectionFile(config.copy(accessToken = "next"))
            next.write()
            first.close()
            assertTrue(Files.exists(file))
            next.close()
            assertFalse(Files.exists(file))
        } finally { directory.toFile().deleteRecursively() }
    }
}
