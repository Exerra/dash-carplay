package com.shilapi.xcertplay.airplay

/** CarPlay's wire appearance values differ from UIKit's light/dark enum. */
internal object CarPlayAppearance {
    const val MANUAL = 1
    fun mode(night: Boolean): Int = if (night) 1 else 0

    fun commands(night: Boolean, cluster: Boolean): List<Map<String, Any?>> {
        val commands = mutableListOf<Map<String, Any?>>(
            linkedMapOf("type" to "setNightMode", "params" to linkedMapOf("nightMode" to night)),
        )
        val displays = if (cluster) listOf(AirPlayInfoPlist.MAIN_UUID, AirPlayInfoPlist.ALT_UUID)
            else listOf(AirPlayInfoPlist.MAIN_UUID)
        for (uuid in displays) {
            for (type in listOf("uiAppearanceUpdate", "mapAppearanceUpdate")) {
                commands.add(linkedMapOf("type" to type, "params" to linkedMapOf(
                    "uuid" to uuid, "appearanceMode" to mode(night), "appearanceSetting" to MANUAL,
                )))
            }
        }
        return commands
    }
}
