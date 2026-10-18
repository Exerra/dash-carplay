package com.playport.server

import org.slf4j.LoggerFactory

data class WifiNetwork(val ssid: String, val channel: Int, val accessPointBssid: String? = null)

/** Reads the current Wi-Fi network on macOS so the wireless bootstrap can hand it to the phone. */
object WifiInfo {
    private val log = LoggerFactory.getLogger("wifi")

    fun detect(config: ServerConfig): WifiNetwork? {
        if (System.getProperty("os.name").lowercase().contains("linux")) {
            val networks = parseLinuxNetworks(runCatching { run("iw", "dev") }.getOrDefault(""))
            val ssid = config.wifiSsid?.takeIf { it.isNotBlank() } ?: networks.singleOrNull()?.ssid ?: return null
            val channel = if (config.wifiChannel > 0) config.wifiChannel
                else networks.filter { it.ssid == ssid }.map { it.channel }.distinct().singleOrNull() ?: 0
            return WifiNetwork(ssid, channel, networks.singleOrNull { it.ssid == ssid }?.accessPointBssid)
        }
        val ssid = config.wifiSsid?.takeIf { it.isNotBlank() } ?: currentSsid()
        if (ssid.isNullOrBlank()) return null
        val channel = if (config.wifiChannel > 0) config.wifiChannel else currentChannel() ?: 0
        return WifiNetwork(ssid, channel)
    }

    /** iw's per-interface blocks; never borrow a channel from a different network. */
    internal fun parseLinuxNetworks(output: String): List<WifiNetwork> {
        val result = mutableListOf<WifiNetwork>()
        var ssid: String? = null
        var channel: Int? = null
        var address: String? = null
        var accessPoint = false
        fun finish() {
            val name = ssid
            val number = channel
            if (name != null && number != null) result.add(WifiNetwork(name, number, if (accessPoint) address else null))
            ssid = null; channel = null
            address = null; accessPoint = false
        }
        for (line in output.lineSequence().map { it.trim() }) {
            when {
                line.startsWith("Interface ") -> finish()
                line.startsWith("ssid ") -> ssid = line.removePrefix("ssid ").takeIf { it.isNotEmpty() }
                line.startsWith("channel ") -> channel = line.removePrefix("channel ").substringBefore(' ').toIntOrNull()
                line.startsWith("addr ") -> address = line.removePrefix("addr ").takeIf { it.matches(Regex("(?i)[0-9a-f]{2}(:[0-9a-f]{2}){5}")) }
                line == "type AP" -> accessPoint = true
            }
        }
        finish()
        return result
    }

    private fun currentSsid(): String? {
        // networksetup on the Wi-Fi hardware port (usually en0).
        wifiHardwarePort()?.let { device ->
            val output = runCatching { run("networksetup", "-getairportnetwork", device) }.getOrNull()
            output?.lineSequence()
                ?.firstOrNull { it.startsWith("Current Wi-Fi Network:") }
                ?.substringAfter(':')
                ?.trim()
                ?.takeIf { it.isNotEmpty() }
                ?.let { return it }
        }
        // Fallback: ipconfig summaries. macOS redacts SSID unless the calling app has Location
        // Services permission, which surfaces here as "<redacted>".
        for (iface in interfaceNames()) {
            val summary = runCatching { run("ipconfig", "getsummary", iface) }.getOrNull() ?: continue
            val ssid = summary.lineSequence()
                .firstOrNull { it.trim().startsWith("SSID :") }
                ?.substringAfter(':')
                ?.trim()
                ?: continue
            when {
                ssid.isEmpty() -> continue
                ssid == "<redacted>" -> {
                    log.warn(
                        "macOS hides the Wi-Fi name from this process (Location Services). " +
                            "Grant Location Services to your terminal, or pass --wifi-ssid and --wifi-passphrase.",
                    )
                    return null
                }
                else -> return ssid
            }
        }
        return null
    }

    private fun wifiHardwarePort(): String? = runCatching {
        val lines = run("networksetup", "-listallhardwareports").lines()
        val index = lines.indexOfFirst { it.trim().equals("Hardware Port: Wi-Fi", ignoreCase = true) }
        if (index >= 0 && index + 1 < lines.size) lines[index + 1].substringAfter(':').trim() else null
    }.getOrNull()

    private fun interfaceNames(): List<String> = runCatching {
        java.net.NetworkInterface.getNetworkInterfaces().toList().map { it.name }
    }.getOrDefault(emptyList())

    private fun currentChannel(): Int? = runCatching {
        val output = run("system_profiler", "SPAirPortDataType")
        Regex("Channel:\\s*(\\d+)").find(output)?.groupValues?.get(1)?.toIntOrNull()
    }.getOrNull()

    private fun run(vararg command: String): String {
        val process = ProcessBuilder(*command).redirectErrorStream(true).start()
        val output = process.inputStream.bufferedReader().readText()
        process.waitFor()
        return output
    }
}
