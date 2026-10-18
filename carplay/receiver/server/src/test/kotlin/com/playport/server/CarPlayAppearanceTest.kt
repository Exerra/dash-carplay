package com.playport.server

import com.shilapi.xcertplay.airplay.AirPlayInfoPlist
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class CarPlayAppearanceTest {
    @get:Rule val temporary = TemporaryFolder()

    @Test fun retainsDashAppearanceBeforeADeviceIsConnectedAndAcrossModeChanges() {
        val config = ServerConfig(identityDir=temporary.newFolder("identity").toPath(),
            stateDir=temporary.newFolder("state").toPath())
        CarPlayServer(config).use { server ->
            val route = server.javaClass.getDeclaredMethod("routeInput", ClientMessage::class.java).apply { isAccessible=true }
            route.invoke(server, ClientMessage(type="night",night=true))
            assertEquals(true, AirPlayInfoPlist.build(server.airPlayConfig)["nightMode"])
            route.invoke(server, ClientMessage(type="night",night=false))
            assertEquals(false, AirPlayInfoPlist.build(server.airPlayConfig)["nightMode"])
            route.invoke(server, ClientMessage(type="night"))
            assertEquals(false, server.airPlayConfig.nightMode)
        }
    }
}
