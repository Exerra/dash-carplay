# Experimental CarPlay status — 18 October 2026

**Confirmed on the test Pi/iPhone:** wireless authentication, CarPlay video,
touch input and music through the Pi's HDMI output. The user confirmed smoother
video after the buffering fix and audible music after the audio-input fix.

The implementation lives in **Dash**. There is no separate workspace-level PlayPort
repository. Its GPL receiver code is vendored as an internal backend under
`carplay/receiver`; the page, display and input are native Qt/GStreamer components
under Dash's `include/carplay`, `src/carplay`, and `app/pages` directories.

## Implemented

- Optional `CARPLAY_BUILD` CMake switch and a CarPlay page appended to the existing
  page list. Existing numeric page and shortcut IDs are preserved.
- Native H.264 decoding, latest-frame rendering, letterboxed touch/mouse mapping,
  two-contact touch messages, knob keys, Home, theme and volume forwarding.
- GPU I420 colour conversion/scaling, retained decoder buffers and shared-storage
  native packet ingestion. Software rendering remains available. The Pi's live
  V3D path and user playback/input/return checks passed; see `PERFORMANCE.md` for
  measured gains, the small-resolution CPU tradeoff and hardware codec limits.
- Native AAC/Opus/PCM playback pipelines. Actual AAC-LC stereo music at 48 kHz
  was received, decrypted and played through HDMI on the Pi.
- Low-latency output buffering and RTSP FLUSH handling to clear queued audio and
  reject stale UDP packets while preserving the session/microphone. Digital-monitor
  Pause tests settled at 74–88 ms without an audible return; the user confirmed
  clean Pause/Resume, track changes and lyrics. See `AUDIO_LATENCY.md` for limits.
- Silent PCM/Opus input for audio-device compatibility without capturing a
  microphone. Real voice input, calls and Siri conversations remain unsupported.
- Receiver start/stop from Dash, authenticated loopback IPC and a private runtime
  connection file. Wi-Fi passwords are passed through the process environment.
- Projection fills the page after the first decoded frame; setup returns on
  disconnect. Settings → CarPlay → Phones & display opens management without
  ending a session, with a Return to CarPlay action.
- CarPlay uses its C/play sidebar icon and appears first in navigation and Layout
  settings. Startup and page cycling follow this order; saved numeric page/action
  IDs and connection service initialization order remain unchanged.
- The OpenDash OEM icon is advertised on the CarPlay home screen; the phone's
  requestUI callback opens native management. Return to CarPlay requests showUI
  and a fresh frame while keeping the phone session alive.
- A three-step iPhone wizard performs discovery/pairing with an in-Dash BlueZ
  confirmation, then explains Wi-Fi joining and CarPlay approval. Saved named
  phones support selection, renaming and removal; one connects at a time.
- Resolution presets, custom even sizes and 30/60 fps persist in Dash settings.
  Apply & reconnect restarts the receiver with those exact display parameters.
- Touch management separates Phones, Display and Setup. Large selectors, an
  on-screen keyboard, validated resolution number pad and swipe scrolling work
  in both themes; controls center on larger displays.
- The 17 original Android Auto action IDs now expose shared Phone controls.
  Keyboard and GPIO bindings route to the active connection with AA priority;
  CarPlay supports navigation, media, wheel, telephony and Siri input. Explicit
  press/release messages preserve held buttons; legacy/browser taps still work.
- Android Auto priority guard (block CarPlay start while AA is active; stop the
  CarPlay receiver if AA connects).
- Settings → Receivers provides independent persistent receiver switches.
  CarPlay applies live and blocks manual/automatic starts while disabled.
  Android Auto applies via a Dash restart and skips worker creation entirely,
  including its USB, TCP and Bluetooth listeners. Layout visibility stays separate.
  The camera initializes QtGStreamer independently, so disabling Android Auto
  does not remove the camera's required type-system initialization.
- Linux BlueZ iAP2 bridge, adapter/device selection and orderly process shutdown.
  Protocol transport preserves byte ordering and has bounded buffering.
- Linux Wi-Fi channel discovery, use of the physical Bluetooth adapter address,
  receiver diagnostics in Dash, build/run scripts and a read-only Pi preflight.
- Source attribution and credentials excluded from the repository.

## Verified on this Mac

| Check | Result |
| --- | --- |
| Receiver JVM build | Passed, Java 21 |
| Protocol tests | 92 passed, no skips |
| Receiver tests | 26 passed, no skips |
| Extracted identity loaded by the JVM and used to sign a challenge | Passed |
| Linux helper's transport/selection/FD tests (no BlueZ hardware) | 6 passed |
| Native Qt 5 client, surface and lifecycle widget | Compiled |
| Native H.264 fixture decode and touch/letterbox test | Passed |
| Android Auto priority and local-endpoint validation tests | Passed |
| Connected-view transitions and returning from management | Passed |
| Two saved phones, selection persistence and display parameters sent to receiver | Passed |
| Wizard steps, duplicate discoveries and pairing-agent phone scoping | Passed |
| Touch selector, keyboard accept/cancel and resolution keypad validation | Passed |
| Shared phone action routing, AA priority and release ownership | Passed |
| Independent receiver settings, restart signaling and disabled CarPlay lifecycle | Passed on Mac and Pi |
| Native control IPC, simultaneous buttons, wheel, focus loss and session reset | Passed |
| Rendered management, wizard and keyboards at 800×480 in both themes | Inspected |
| Bundled OEM PNG and its /info advertisement | Passed |
| Real backend startup/status/private-IPC/MFi-SAP response smoke test | Passed |
| macOS Bluetooth helper | Compiled; no pairing attempted |
| Optional browser diagnostic client | Bun build/input checks passed |

The native test uses an actual GStreamer encoder/decoder and a synthetic local
WebSocket peer, including a burst of 60 dependent H.264 frames. The smoke test
uses the actual extracted identity and RTSP responder, with discovery and
Bluetooth disabled. Separate hardware tests below establish phone acceptance.

Qt 5, GStreamer and CMake were installed on the Mac to compile and test the native
components. The complete Dash executable was not built here: its existing
Linux-only Android Auto/BlueZ dependency stack still needs the target environment.

## Verified on the Pi

- Receiver enablement was checked in all four combinations: TCP 5000 was
  present only with Android Auto enabled, and CarPlay RTSP 7000 only with
  CarPlay enabled. Both listeners were absent with both disabled. Settings
  persisted across Dash starts; applying an Android Auto change also restarted
  Dash successfully without changing its systemd PID. Final development settings
  are Android Auto off / CarPlay on, with real iPhone video reconnected.
- Disabling Android Auto exposed camera dependencies on QtGStreamer setup and
  early Qt Quick renderer creation. The camera now owns both initialization
  steps. The user confirmed a normal USB webcam picture after the fix with
  Android Auto disabled; Dash stayed running and CarPlay remained available.

- The CarPlay sidebar icon and first-page order were built and visually checked
  on the Pi. Dash opened directly into the reconnected CarPlay session, with the
  new icon first in the sidebar; the persistent page registry is unchanged.
- Shared Phone controls compiled in the full Dash application; native routing,
  control IPC, input lifecycle and configuration teardown checks passed. The
  receiver suite passed 95 protocol tests and 26 server tests; one external MFi
  identity test was skipped because its separate test path was not configured.
  The updated receiver authenticated the existing iPhone and decoded live video.
  The user confirmed the existing navigation, select and scroll keybindings work
  with CarPlay on the Pi. Media/voice/call hardware acceptance remains separate.
- The touch redesign was built and installed, native interaction and shutdown
  checks passed, and the user confirmed the new controls look good and work.
- The updated connected view was captured on the Pi with setup fully hidden, and
  the user confirmed management and returning to CarPlay work. A direct test of
  the wizard's real Bluetooth controller found the existing paired iPhone. A
  second, newly paired phone still needs end-to-end acceptance.
- The user confirmed the OpenDash icon appears inside CarPlay, opens the native
  management screen and returns to the CarPlay picture successfully.

- Raspberry Pi 5, 4 GB RAM, aarch64, Debian/Raspberry Pi OS 12 Bookworm; existing
  OpenAuto/AASDK and QtGStreamer installations reused.
- Native Qt/GStreamer tests passed on the target, including actual H.264 fixture
  decoding and input mapping. Six Linux bridge tests also passed.
- Java 21 runtime installed from a checksum-verified Temurin archive; backend
  startup, private IPC, HTTP status and MFi-SAP responder smoke test passed.
- BlueZ accepts iAP2 profile registration as the normal `pi` user. Actual iPhone
  RFCOMM connection, accessory identification and MFi authentication succeeded.
  The success callback runs only after the phone sends AuthenticationSucceeded
  (`0xaa05`). This establishes acceptance of this identity by this test phone.
- A 5 GHz NetworkManager hotspot runs at `10.42.0.1` on channel 36; Ethernet remains
  available. An altered `libnl-route` library had broken the Wi-Fi service; the
  matching distribution packages were restored and the service now starts.
- Phone approved its CarPlay prompt, opened an authenticated AirPlay connection
  and an iAP2 Wi-Fi tunnel, and sent H.264 video. The user confirmed visible video
  and working touch input in Dash. Initial video was very choppy; audio stayed on the phone
  and OpenDash was absent from the phone's audio output picker.
- Full Dash executable built successfully with CarPlay enabled and launched on
  the Pi's Wayland display. The native connection page starts its receiver and
  reports authentication progress. Original binary/config backups are retained
  on the Pi outside the checkout.
- Added discovery of the phone's Bonjour CarPlay control endpoint and an explicit
  connect invitation, restricted to the selected phone's Bluetooth ID. Tests use
  a real TCP peer and cover interface scoping for IPv6 link-local addresses.
- Linux AP discovery now supplies the hotspot's actual BSSID in the Wi-Fi handoff.
  IPv6 link-local was enabled on the hotspot for the next connection experiment.
- Linux wireless listeners, service publication and control discovery now retain
  both usable address families of the selected interface. IPv6 scope stays local
  and is omitted from the endpoint sent to the phone. Address-selection tests
  cover the IPv4/IPv6 pairing and exclude unusable alternate addresses.
- An opt-in `CARPLAY_AUTOSTART=1` setting starts the backend during page
  initialization, still subject to Android Auto priority.
- Latest target build and native tests passed. One receiver process serves both
  hotspot address families; local RTSP OPTIONS requests returned 200 over each.
  Subsequent phone connections established a real media session.
- User reports an iPhone 14 on iOS 26.5.2 with Siri enabled. Discovery capability
  bits were inconsistent with the receiver's `/info` response; both now derive
  from the same capability value, with a regression test covering audio modes.
- Closing Dash using X reproduced an existing invalid free in QApplication
  teardown: its QObject-owned configuration singleton occupied static storage.
  It now uses heap storage matching Qt ownership. A small lifetime test failed
  before the change and passes afterward on the Pi. OpenAuto shutdown also needed
  its I/O service stopped before joining threads and a shorter USB event timeout.
  Full Dash then exited normally under the debugger. AA-connected shutdown still
  needs a separate hardware check.
- The Pi reported approximately 86°C with active throttling during the poor-video
  session, later cooling to 77°C. No thermal limits were changed.
- Video buffering now preserves compressed reference frames, uses slice decoding
  without frame-thread latency, copies to Qt's native 32-bit image format and
  repaints only when a frame changes. Native diagnostics count received/decoded
  frames and queued bytes. The 60-frame decode test passes on Mac and Pi. The user
  confirmed smoother video after deployment; live counters showed approximately
  30 received/decoded frames per second during animation with no input backlog.
  Subsequent temperature fell to about 75°C, with Dash's average CPU lower.
- The receiver now honors the phone's `disableBluetooth` handoff request only
  after the same session's authenticated Wi-Fi tunnel is ready. Previously it
  retained the bootstrap indefinitely. Three tests cover ordering, duplicate
  signals and stale sessions. Real logs confirm the handoff completes; audio was
  still absent afterward.
- The phone's mode reports showed accessory ownership of main audio. Initial
  modes now release that ownership instead of reserving audio for local head-unit
  playback. A regression test preserves audio-format advertisement and verifies
  the release. This change alone did not restore audio.
- Audio became available after advertising a complete input/output device with
  Opus input capability. The PCM-only trial did not restore the route. The native
  launcher enables `--silent-microphone`; negotiated input receives timed encrypted
  PCM or Opus silence without opening a capture device. UDP tests verify payload
  encryption, RTP timing and teardown. The Opus silence frame was also decoded on
  the Pi to 960 zero PCM samples. This addresses the same symptom described in
  [upstream issue 288](https://github.com/shihabal3amri/DiPlay/issues/288).
- The phone then opened a type-102 AAC-LC 48 kHz stereo stream. Logs showed
  successfully decrypted music packets without authentication failures, and
  PipeWire showed Dash playing to HDMI. Dash's volume was initially 0%; after
  raising it, the user confirmed audible music and the working CarPlay route.

## Remaining hardware gates

1. Check long-session CPU/latency and reconnect behavior after a full Pi reboot.
2. Test switching between Android Auto and CarPlay with a real Android phone.
3. Complete new-device pairing and switching with a second physical iPhone; the
   paired-phone discovery path and two-profile persistence/routing are tested.
4. Measure higher resolutions/60 fps on the Pi; the 800×480/30 fps mode remains
   the default, and exact selected dimensions/rate are verified in receiver-launch tests.

Real microphone capture/Siri conversations, echo cancellation, phone-call acceptance,
automatic AP management inside Dash and wired USB are outside this first prototype.
The current receiver uses an OS-managed Wi-Fi LAN or hotspot. The initial mode is
H.264, 800×480 at 30 fps.

The confirmed working Pi executable, receiver distribution, launcher and Dash
settings were saved under
`/home/pi/dash-backups/carplay-working-2026-10-18-fxL873`, with SHA-256 checksums.
Identity files and the private environment remain at their existing runtime paths.
Use the `OpenDash-CarPlay` desktop shortcut to reopen this development setup.
The subsequent wizard, display settings and confirmed in-CarPlay return icon build
is saved separately at `/home/pi/dash-backups/carplay-ui-2026-10-18-bO7Yoq`.
The touch redesign executable, configuration and UI sources are saved at
`/home/pi/dash-backups/carplay-touch-ui-2026-10-18-fq3wLO` with SHA-256 checksums.
The shared-control app, receiver and settings are saved at
`/home/pi/dash-backups/carplay-shared-controls-2026-10-18-Xa1rzr`.
The GPU renderer, native media sources, benchmark results and settings are saved
at `/home/pi/dash-backups/media-optimized-2026-10-18-PTASWZ`.

See `README.md` for build/run instructions. No credentials are committed or bundled
in the receiver source; the extracted identity remains in the workspace's external
`runtime-assets` directory.
