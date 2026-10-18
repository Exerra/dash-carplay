package com.playport.server

import com.shilapi.xcertplay.airplay.AirPlayInfoPlist
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class CarPlayAppearanceTest {
    @get:Rule val temporary = TemporaryFolder()

    @Test fun microphoneModesChangeCapabilitiesWithoutRemovingAudioOutput() {
        for (mode in listOf("silent","phone","real")) {
            val config = ServerConfig(identityDir=temporary.newFolder("identity-"+mode).toPath(),
                stateDir=temporary.newFolder("state-"+mode).toPath(),microphoneMode=mode,silentMicrophone=true)
            assertEquals(mode!="phone", config.offersMicrophone)
            assertEquals(mode=="silent", config.usesSilentMicrophone)
            CarPlayServer(config).use { server ->
                val formats = AirPlayInfoPlist.build(server.airPlayConfig)["audioFormats"] as List<*>
                assertTrue(formats.isNotEmpty())
                assertEquals(mode!="phone",formats.any { (it as Map<*, *>).containsKey("audioInputFormats") })
            }
        }
    }

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
