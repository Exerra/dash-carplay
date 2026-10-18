package com.playport.server

import com.shilapi.xcertplay.airplay.AirPlayInfoPlist
import java.io.ByteArrayInputStream
import javax.imageio.ImageIO
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class OemReturnIconTest {
    @get:Rule val temporary = TemporaryFolder()

    @Test fun realBundledIconIsAdvertisedForReturningToOpenDash() {
        val config = ServerConfig(deviceName = "OpenDash",
            identityDir = temporary.newFolder("identity").toPath(), stateDir = temporary.newFolder("state").toPath())
        CarPlayServer(config).use { server ->
            val info = AirPlayInfoPlist.build(server.airPlayConfig)
            assertEquals(true, info["oemIconVisible"])
            assertEquals("OpenDash", info["oemIconLabel"])
            val icon = (info["oemIcons"] as List<*>).single() as Map<*, *>
            val image = ImageIO.read(ByteArrayInputStream(icon["imageData"] as ByteArray))
            assertNotNull(image)
            assertEquals(200, image.width)
            assertEquals(200, image.height)
        }
    }
}
