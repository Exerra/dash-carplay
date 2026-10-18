package com.playport.server

import io.ktor.server.engine.applicationEnvironment
import io.ktor.server.engine.embeddedServer
import io.ktor.server.engine.sslConnector
import io.ktor.server.netty.Netty
import java.nio.file.Path
import kotlin.system.exitProcess
import org.slf4j.LoggerFactory

fun main(args: Array<String>) {
    val log = LoggerFactory.getLogger("playport")
    val identityDir = Path.of("identity").toAbsolutePath().normalize()
    val stateDir = Path.of(System.getProperty("user.home"), ".playport")

    // First pass: honour --state-dir/--identity-dir before looking for a saved config.
    val bootstrap = ServerConfig.fromArgs(args, environmentDefaults(ServerConfig(identityDir = identityDir, stateDir = stateDir)))
    val store = ConfigStore(bootstrap.stateDir)
    val saved = store.load()
    // Second pass: saved settings are defaults; CLI flags still win.
    val config = saved?.let { persisted ->
        ServerConfig.fromArgs(args, environmentDefaults(persisted.toServerDefaults(bootstrap.identityDir, bootstrap.stateDir)))
    } ?: bootstrap

    if (saved != null) {
        log.info("loaded settings from {}", bootstrap.stateDir.resolve("config.json"))
    }

    val server = CarPlayServer(config, store)
    try {
        server.mfi
    } catch (error: Exception) {
        System.err.println("MFi identity unavailable: ${error.message}")
        System.err.println(
            "Provide identity/offline-mfi/{identity.pk8,certificate.p7b} or configure --mfi-server.",
        )
        exitProcess(1)
    }

    val webRoot = Path.of("web/dist").toAbsolutePath().normalize()
    val tls = TlsCertificate.loadOrCreate(config.stateDir, server.bindAddresses)
    val http = if (config.insecureHttp) {
        embeddedServer(Netty, port = config.httpPort, host = config.webBindAddress) {
            server.hub.install(this, webRoot)
        }
    } else {
        embeddedServer(
            Netty,
            environment = applicationEnvironment { },
            configure = {
                sslConnector(tls.keyStore, tls.alias, { tls.password }, { tls.password }) {
                    host = config.webBindAddress
                    port = config.httpPort
                }
            },
        ) {
            server.hub.install(this, webRoot)
        }
    }

    try {
        server.start()
    } catch (error: Exception) {
        System.err.println("Could not start the AirPlay listener: ${error.message}")
        exitProcess(1)
    }
    http.start(wait = false)
    val connectionFile = ConnectionFile(config).also { it.write() }

    val scheme = if (config.insecureHttp) "http" else "https"
    val url = "$scheme://${config.webBindAddress}:${config.httpPort}/?token=${config.accessToken}"
    println()
    println("  PlayPort ${projectVersion()}")
    println("  ------------------------------------------------------------------")
    println("  CarPlay device name : ${config.deviceName}")
    println("  AirPlay listener    : ${server.bindAddress.hostAddress}:${config.airPlayPort}")
    println("  Browser UI          : $url")
    if (!config.insecureHttp) {
        println("  (accept the self-signed certificate warning once; WebCodecs needs HTTPS)")
    }
    println("  Pairing PIN         : ${CarPlayServer.PAIRING_PIN}")
    println("  Wireless bootstrap  : ${if (config.wireless) "enabled" else "disabled (start with --wireless)"}")
    println("  ------------------------------------------------------------------")
    println("  On the iPhone: Settings > General > CarPlay > available cars, then")
    println("  enter PIN ${CarPlayServer.PAIRING_PIN} when asked.")
    println()

    Runtime.getRuntime().addShutdownHook(
        Thread {
            log.info("shutting down")
            runCatching { server.close() }
            runCatching { http.stop(1_000, 3_000) }
            runCatching { connectionFile.close() }
        },
    )
    Thread.currentThread().join()
}

private fun projectVersion(): String =
    CarPlayServer::class.java.`package`?.implementationVersion ?: "0.1.0"

private fun environmentDefaults(config: ServerConfig): ServerConfig = config.copy(
    wifiSsid = System.getenv("CARPLAY_WIFI_SSID") ?: config.wifiSsid,
    wifiPassphrase = System.getenv("CARPLAY_WIFI_PASSPHRASE") ?: config.wifiPassphrase,
    bluetoothDeviceAddress = System.getenv("CARPLAY_BT_ADDRESS") ?: config.bluetoothDeviceAddress,
)
