# Experimental CarPlay in Dash

This adds an optional **native CarPlay page to Dash**. The bundled Kotlin/JVM
receiver is a backend component: it handles the phone protocol and sends media to
Dash over an authenticated loopback WebSocket. Dash decodes H.264/audio with
GStreamer and renders through Qt. A browser is not needed.

Current scope: same-LAN wireless bootstrap, native video, touch/mouse/knob input,
home control, theme and volume, and decoded audio output. Real microphone capture, echo
cancellation, automatic hotspot setup and wired USB are not implemented in the
native integration. A Pi 5 has authenticated the test iPhone and displayed real
CarPlay video with working touch and HDMI music playback. The user confirmed
smoother video after the buffering fix. See `STATUS.md` for validation limits.
Android Auto has priority: starting CarPlay is blocked while AA is connected, and
an incoming AA connection stops the CarPlay backend.

## Build

The existing Dash build dependencies remain required. Add a Java 21+ JDK, Python
3.9+, BlueZ and `dbus-next` for the receiver. The native page needs GStreamer app,
video, `avdec_h264`/H.264 parser and audio plugins (including AAC/Opus decoding).
The first hardware target runs 64-bit Raspberry Pi OS Bookworm. Its existing Dash
dependencies were reused; `python3-dbus-next` supplies the Linux bridge dependency.
Java 21 is installed separately because its configured Bookworm repositories do
not provide it.

```sh
cd dash
./carplay/build.sh
cmake -S . -B build -DCARPLAY_BUILD=ON -DGST_BUILD=ON -DRPI_BUILD=ON
cmake --build build
```

CarPlay is **off by default** in CMake; existing Android Auto builds do not gain a
Java runtime requirement unless this option is enabled. The receiver source and
upstream notices are in `carplay/receiver`; see `UPSTREAM.md` for provenance.
No code from the proprietary SDK is included.

If the optional browser diagnostics client is wanted, use Bun only:

```sh
cd carplay/receiver/web
bun install
bun run build
bun run test
```

## First Pi run

1. Set up the Python environment as described in `receiver/linux/README.md`.
2. Put the Pi and iPhone on the same Wi-Fi LAN (multicast/client communication enabled).
3. Supply the identity folder containing `offline-mfi/identity.pk8` and
   `offline-mfi/certificate.p7b`. Credentials stay outside source control. The
   workspace's previously extracted `runtime-assets` directory has this layout.
4. Run the read-only preflight check:

```sh
python3 carplay/preflight.py --identity-dir /absolute/path/to/runtime-assets
```

5. For a new installation, enter the identity folder and shared Wi-Fi details in
   **Setup**. The configured development Pi already has these.
6. **Add iPhone** guides you through pairing, Wi-Fi and CarPlay approval. Keep the
   iPhone's Bluetooth settings open and confirm the matching code when prompted;
   no Bluetooth address needs to be typed. Select a saved phone and choose **Connect**.

## Phones and display

CarPlay appears first in the sidebar and Layout settings, with its own CarPlay
icon. When enabled, it is also the initial page. Saved page IDs, enabled states
and keybindings stay unchanged; page cycling follows the visible sidebar order.

Once the first video frame arrives, the setup screen disappears and CarPlay fills
the page, like Android Auto. Disconnecting returns to the setup screen.
While connected, use **Settings → CarPlay → Phones & display** to manage the
connection. **Return to CarPlay** resumes the picture without disconnecting.
The **OpenDash** icon on the CarPlay home screen also opens this management screen
using CarPlay's OEM return action. Its icon reuses Dash's existing logo. This does
not require installing an iPhone app.

Management has separate **Phones**, **Display** and **Setup** screens, with large
touch targets and swipe scrolling. Tap a selector to open a large list; tap a name
or Wi-Fi field for the on-screen keyboard. Custom dimensions use a number pad
that accepts only valid even pixel sizes. The layout supports 800×480 screens and
centers the controls on larger displays. Both light and dark Dash themes apply.
Buttons, selected tabs and phones, focus borders and keyboard selections use
Dash's configured accent colour. Changes apply immediately, including to an open
wizard or dialog; text on accent backgrounds switches between black and white
for contrast.
Dash also forwards its light/dark mode to CarPlay's interface and maps, using
both the night-mode signal and per-display appearance updates. The receiver
retains it between phone connections, advertises the current mode in the initial
configuration, and sends live changes over the control channel.

- Save several named iPhones, rename or remove them under **Phone options**, and choose which phone to
  connect. One phone is active at a time. Removing a saved entry keeps its system
  Bluetooth pairing; the wizard can add it again.
- Choose 800×480, 1024×600, 1280×720, 1920×1080, 1920×720, or a custom even size
  between 480 and 3840 pixels per edge. Frame rate can be 30 or 60 fps.
- **Apply & reconnect** saves display settings and restarts an active connection.
  With CarPlay stopped, it saves them for the next connection. The default remains
  800×480 at 30 fps; higher settings still need performance checks on the target.

Phone names, selection and display settings persist in Dash settings. The setup
wizard performs pairing on the Pi through BlueZ and shows confirmation inside
Dash; ordinary connection screens do not expose Bluetooth addresses or identity
paths. Those technical settings remain under **Setup**.

The native page does not save the Wi-Fi password in Dash settings; it passes it to
the backend's environment for that run. Pairing and generated receiver state live
under `${XDG_STATE_HOME:-~/.local/state}/opendash/carplay`. The local connection
token file has owner-only permissions. The frontend initially requests H.264,
800×480 at 30 fps. Decoder output retains the latest I420 buffer; OpenGL converts
and scales it on the GPU, with a software fallback for headless/unsupported
graphics. See [PERFORMANCE.md](PERFORMANCE.md) for the pipeline audit, measured
before/after results, benchmark commands and thermal/measurement limits.

## Receiver switches

**Settings → Receivers** has independent Android Auto and CarPlay switches. Both
default to enabled and the selections persist across restarts. Layout page
visibility is separate: hiding a page does not turn its receiver off.

CarPlay changes take effect with **Apply**, stopping the receiver, automatic
reconnection and discovery. Its saved phones, identity and display settings are
kept. Disabled CarPlay cannot be started with Connect or the launcher's autostart.

Android Auto changes use **Apply & restart Dash**. Only Dash restarts, preserving
its launcher environment and systemd process identity; the Pi does not reboot.
When disabled, the OpenAuto worker is not created, so no Android Auto USB probing,
TCP listener or wireless Bluetooth bootstrap starts. A full restart is needed
because the current OpenAuto dependency retains listeners after `App::stop()`.

The switches control Dash's receivers; the system's shared Wi-Fi hotspot and
Bluetooth radio stay available for the other receiver and ordinary Bluetooth use.

## Shared phone controls

For wireless CarPlay music, Dash uses a 60 ms output-buffer request and clears
queued sound when the iPhone sends FLUSH for playback changes. See
[AUDIO_LATENCY.md](AUDIO_LATENCY.md) for measured response, validation limits and
the `CARPLAY_AUDIO_BUFFER_MS` compatibility override.

In **Settings → Actions**, the former Android Auto actions are now named **Phone**
actions. Existing keyboard shortcuts and GPIO assignments keep their original
numeric IDs and automatically control the active Android Auto or CarPlay phone.
Android Auto retains priority if both connections overlap; with neither phone
connected the actions do nothing. Media actions work while another Dash page is
visible, including CarPlay management.

| Action | CarPlay input |
| --- | --- |
| Left / Right / Up / Down | Controller direction |
| Enter / Back / Home | Select / Back / Home |
| Play / Pause / Toggle Play | Media play / pause / toggle |
| Prev Track / Next Track | Media previous / next |
| Scroll Up / Scroll Down | Rotary wheel movement |
| Phone / Answer Call | Telephony hook switch (answer) |
| Call End | Telephony drop |
| Voice | Siri button |

Single-key bindings send separate press and release events. GPIO and shortcut
chords send complete taps. The CarPlay event channel preserves held states, and
disconnecting clears local held buttons. Unassigned arrow, Enter, Escape, Home
and media keys also work directly when the CarPlay picture has keyboard focus.
Voice and call button routing does not add real microphone capture; the prototype
still supplies silent input as described below.

The launcher offers a silent PCM/Opus microphone input because the test iPhone
would not route music to an accessory without input capability. It captures no
sound; a real microphone still needs to be implemented for calls and Siri.
Dash's bottom volume slider controls playback; 0% silences both Dash and HDMI.

On the configured development Pi, open the **OpenDash-CarPlay** desktop shortcut
to launch with its saved private environment. The equivalent terminal command is:

```sh
/home/pi/dash/carplay/runtime/launch-dash.sh
```

Keep the iPhone on **OpenDash-CarPlay** Wi-Fi and use Dash's CarPlay page. Avoid
starting a second receiver while one is running. This setup does not automatically
launch Dash at boot; Bluetooth pairing, the identity files and the private launcher
environment remain on the Pi.

The CMake runner path points to this source checkout. For a relocated installation,
keep the complete `carplay` directory together and set the Dash QSettings key
`CarPlay/receiver_command` to the absolute path of its `run.sh`.

For a standalone backend diagnostic run from the Dash directory:

```sh
CARPLAY_IDENTITY_DIR=/absolute/path/to/runtime-assets \
CARPLAY_WIFI_SSID='Your network' \
CARPLAY_WIFI_PASSPHRASE='Your password' \
./carplay/run.sh --bind <Pi-LAN-address> --bt-address AA:BB:CC:DD:EE:FF
```

When the Pi has both Ethernet and a Wi-Fi hotspot, set `CARPLAY_BIND_ADDRESS` to
the hotspot's address before launching Dash (for example `10.42.0.1`). This keeps
the phone handoff on the interface the iPhone can reach. `CARPLAY_WIFI_SSID` and
`CARPLAY_WIFI_PASSPHRASE` also prefill the native connection form; the password is
still not saved in Dash settings. The hotspot itself is managed by the OS.
On Linux the receiver listens and advertises on both usable address families of
that selected interface. A scoped IPv6 bind such as `fe80::1234%wlan0` is also
supported; the local scope suffix is removed from the address sent to the phone.
`CARPLAY_AUTOSTART=1` starts the backend when Dash initializes the CarPlay page;
the Android Auto priority guard still applies.

The password environment variable can also be set without writing it in shell
history. The web/native API binds only to loopback by default; the AirPlay listener
uses the selected LAN address. The optional diagnostics UI requires a built web
client and is available on localhost with the generated token.

## Local checks before hardware is available

```sh
cd carplay/receiver
PLAYPORT_TEST_IDENTITY_DIR=/absolute/path/to/runtime-assets ./gradlew build :server:installDist
.venv/bin/python -m unittest discover -s linux -v
```

The credential test signs a challenge and checks it against the supplied
certificate. This proves key consistency, not acceptance by iOS.

The real receiver can also be exercised without discovery, Bluetooth or a phone:

```sh
python3 carplay/smoke-test.py --identity-dir /absolute/path/to/runtime-assets
```

Run that command from the Dash directory. It starts a temporary loopback receiver,
checks its private native connection file, queries status, and sends an MFi-SAP
request through the actual RTSP server. The process and state are removed afterward.

The actual native client/surface can also be built without the rest of Dash or its
Linux-only Android Auto dependencies:

```sh
cmake -S carplay/native-tests -B build/carplay-native-tests
cmake --build build/carplay-native-tests
ctest --test-dir build/carplay-native-tests --output-on-failure
```

On a Linux system with Dash's OpenAuto dependencies installed, add
`-DCARPLAY_TEST_DASH_SHUTDOWN=ON` to the native-test CMake command to include the
configuration singleton's QApplication teardown regression test.

That test runs a local WebSocket fixture, generates an H.264 test pattern, exercises
the real GStreamer decoder and checks input coordinates/letterboxing. It needs
Qt 5 Test and the GStreamer `x264enc` test-fixture encoder in addition to the normal
native dependencies. It does not claim an end-to-end phone connection.
On macOS the test limits plugin scanning to its required GStreamer modules, avoiding
unrelated hardware/Python plugin probes in Homebrew's bundled plugin directory.
