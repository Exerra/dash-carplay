# Receiver provenance

The receiver in `receiver/` is vendored from https://github.com/youcci/playport,
revision `9a0882dd0ffe48e467b59d58b12d81391df55ade` (17 October 2026 checkout).
It is a GPL-3.0 desktop adaptation of DiPlay/xcertplay. Its license, attribution,
and original documentation are preserved under that directory.

OpenDash changes add a Linux Bluetooth transport, native Qt/GStreamer integration,
and build/run helpers. The Wi-Fi handoff includes the AP's BSSID when Linux exposes
it, and Bonjour control discovery/invitation follows the DiPlay connection flow.
The Kotlin process is a backend component of Dash; it is
not a separate head-unit application. The browser client is optional diagnostics.

The proprietary SDK under the workspace's `Carplay/` directory is not included.
Accessory credentials are supplied externally and are never part of these sources.
