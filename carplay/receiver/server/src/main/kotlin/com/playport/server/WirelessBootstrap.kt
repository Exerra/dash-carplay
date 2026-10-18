package com.playport.server

import com.shilapi.xcertplay.iap2.session.Iap2Session
import com.shilapi.xcertplay.mfi.Iap2MfiAuthenticationClient
import com.shilapi.xcertplay.transport.Iap2IdentificationConfig
import com.shilapi.xcertplay.transport.Iap2WirelessCarPlayEndpoint
import com.shilapi.xcertplay.transport.Iap2WirelessControlClient
import com.shilapi.xcertplay.transport.Iap2WirelessControlTerminal
import com.shilapi.xcertplay.transport.Iap2WirelessIdentification
import java.io.Closeable
import java.nio.file.Path
import org.slf4j.LoggerFactory

/**
 * Wireless CarPlay bring-up: Bluetooth RFCOMM iAP2 bootstrap, Wi-Fi credentials handoff and the
 * tunneled iAP2 handover.
 *
 * Requires the macOS `bt-bridge` helper (macos/bt-bridge) with the iPhone already paired in
 * System Settings, and the server running on the Wi-Fi network the phone should join.
 */
class WirelessBootstrap(private val server: CarPlayServer) : Closeable {
    private val log = LoggerFactory.getLogger("wireless")
    @Volatile private var bridgeProcess: ProcessByteStream? = null
    @Volatile private var stopped = false
    private val resourceLock = Any()
    @Volatile var running = false
        private set

    fun start() {
        synchronized(resourceLock) {
            if (stopped || running) return
            running = true
        }
        Thread(::run, "wireless-bootstrap").apply {
            isDaemon = true
            start()
        }
    }

    private fun run() {
        var session: Iap2Session? = null
        try {
            if (stopped) return
            server.hub.setReceiverStatus("Preparing wireless connection")
            val config = server.config
            val wifi = WifiInfo.detect(config)
            if (wifi == null) {
                server.hub.setReceiverStatus("Enter the Wi-Fi network name before starting CarPlay.")
                log.warn(
                    "wireless bootstrap skipped: no Wi-Fi network detected. " +
                        "Pass the Wi-Fi the iPhone should use: " +
                        "--wifi-ssid \"<name>\" --wifi-passphrase \"<password>\" " +
                            "Automatic Wi-Fi discovery depends on the host platform.",
                )
                return
            }
            if (!wifi.ssid.isNullOrBlank() && config.wifiPassphrase.isNullOrBlank()) {
                log.warn("no --wifi-passphrase configured; the phone must already know network {}", wifi.ssid)
            }
            val bridgePath = BridgePaths.resolve(config.bluetoothBridge)
            if (bridgePath == null) {
                server.hub.setReceiverStatus("Bluetooth bridge unavailable. Check the receiver setup.")
                log.warn(
                    "wireless bootstrap skipped: bt-bridge helper not found; " +
                        "see linux/README.md or macos/bt-bridge/build.sh, or pass --bt-bridge",
                )
                return
            }
            val deviceAddress = config.bluetoothDeviceAddress?.takeIf { it.isNotBlank() }
                ?: BluetoothBridge.findPairedIphone(bridgePath)
            if (deviceAddress == null) {
                server.hub.setReceiverStatus("Pair an iPhone in Bluetooth settings, or choose its Bluetooth address.")
                log.warn("wireless bootstrap skipped: no paired iPhone found; pass --bt-address AA:BB:CC:DD:EE:FF")
                return
            }
            val hostBluetoothMac = server.bluetoothAddress
            server.controlPhoneAddress = deviceAddress
            log.info("wireless bootstrap: iPhone={} hostBt={} wifi={} ch={}", deviceAddress, hostBluetoothMac, wifi.ssid, wifi.channel)

            if (stopped) return
            val stream = BluetoothBridge.open(bridgePath, deviceAddress)
            synchronized(resourceLock) {
                if (stopped) { stream.close(); return }
                bridgeProcess = stream
            }
            val channel = Iap2Session.openWireless(stream, traceContext = "wireless-rfcomm", onTrace = { log.debug(it) })
                .also { session = it }
            log.info("wireless iAP2 CSM channel opened over RFCOMM")
            server.hub.setReceiverStatus("Authenticating iPhone")

            val identification = Iap2IdentificationConfig(
                name = server.config.deviceName,
                modelIdentifier = server.config.model,
                manufacturer = server.config.manufacturer,
                serialNumber = "CARPLAYWEB-" + server.deviceId.replace(":", ""),
                firmwareVersion = "1.0.0",
                hardwareVersion = "1.0",
                wireless = Iap2WirelessIdentification(hostBluetoothMac, wifi.ssid),
            )
            val endpoint = Iap2WirelessCarPlayEndpoint(
                ssid = wifi.ssid,
                passphrase = config.wifiPassphrase.orEmpty(),
                channel = wifi.channel,
                security = config.wifiSecurity,
                ipAddresses = server.bindAddresses.map { it.hostAddress.substringBefore('%') },
                airPlayPort = config.airPlayPort,
                deviceIdentifier = server.deviceId,
                publicKey = server.identity.publicKeyHex,
                sourceVersion = config.sourceVersion,
                accessPointBssid = wifi.accessPointBssid?.split(':')?.map { it.toInt(16).toByte() }?.toByteArray(),
            )
            server.media.setIapTunnelHandler { tunnelStream ->
                server.handleWirelessTunnel(tunnelStream, identification, endpoint)
            }
            val result = Iap2WirelessControlClient(
                session = channel,
                mfi = Iap2MfiAuthenticationClient(server.mfi),
            ).run(
                identification = identification,
                endpoint = endpoint,
                timeoutMillis = Iap2WirelessControlClient.NO_TIMEOUT_MILLIS,
                onReady = {
                    log.info("wireless bootstrap accepted by the iPhone; waiting for the Wi-Fi handoff")
                    server.hub.setReceiverStatus("iPhone authenticated. Waiting for CarPlay connection.")
                },
                onProgress = { log.debug("wireless $it") },
            )
            if (!stopped) {
                when (result.terminal) {
                    Iap2WirelessControlTerminal.CHANNEL_CLOSED ->
                        log.info(
                            "wireless RFCOMM channel closed stage={} handoffSeen={}",
                            result.stage,
                            result.wirelessCarPlayAvailableSeen,
                        )
                    Iap2WirelessControlTerminal.TIMED_OUT -> log.info("wireless bootstrap timed out")
                }
            }
        } catch (error: Exception) {
            if (!stopped) {
                log.warn("wireless bootstrap failed: {}", error.message)
                server.hub.setReceiverStatus("CarPlay connection failed: ${error.message}")
            }
        } finally {
            val stream = synchronized(resourceLock) {
                bridgeProcess.also { bridgeProcess = null; running = false }
            }
            stream?.close()
            session?.close()
        }
    }

    override fun close() {
        val stream = synchronized(resourceLock) {
            stopped = true
            bridgeProcess.also { bridgeProcess = null }
        }
        stream?.close()
    }
}

/** Helpers around the macOS bt-bridge executable. */
object BluetoothBridge {
    private val log = LoggerFactory.getLogger("bt-bridge")

    fun localAddress(bridgePath: Path): String? = runCatching {
        val output = metadata(bridgePath, "--print-address")?.trim() ?: return@runCatching null
        output.takeIf { it.matches(Regex("[0-9A-Fa-f:]{17}")) }
    }.getOrNull()

    fun findPairedIphone(bridgePath: Path): String? = runCatching {
        val lines = metadata(bridgePath, "--list")?.lines() ?: return@runCatching null
        lines.filter { it.contains("iphone", ignoreCase = true) }.singleOrNull()
            ?.substringBefore(' ')
            ?.trim()
            ?.takeIf { it.matches(Regex("[0-9A-Fa-f:]{17}")) }
    }.onFailure { log.warn("paired-device lookup failed: {}", it.message) }
        .getOrNull()

    fun open(bridgePath: Path, deviceAddress: String): ProcessByteStream {
        val process = ProcessBuilder(bridgePath.toString(), "--address", deviceAddress)
            .redirectErrorStream(false)
            .start()
        return ProcessByteStream(process, "bt-bridge")
    }

    private fun metadata(bridgePath: Path, flag: String): String? {
        val process = ProcessBuilder(bridgePath.toString(), flag)
            .redirectError(ProcessBuilder.Redirect.DISCARD).start()
        try {
            if (!process.waitFor(10, java.util.concurrent.TimeUnit.SECONDS)) return null
            if (process.exitValue() != 0) return null
            return process.inputStream.readNBytes(65536).toString(Charsets.UTF_8)
        } finally {
            if (process.isAlive) process.destroyForcibly()
        }
    }
}
