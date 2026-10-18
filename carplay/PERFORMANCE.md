# Media pipeline audit and optimization — 18 October 2026

Target: Raspberry Pi 5, 4 GB, 64-bit Raspberry Pi OS Bookworm, GStreamer 1.22,
Qt 5, Wayland, V3D 7.1.7.0. Production Dash is a Release build. Resolution,
frame-rate selections and receiver enablement were preserved.

## Where acceleration is actually used

| Stage | Actual implementation on this Pi |
| --- | --- |
| Phone video encoding | Done by the phone; Dash does not re-encode video. The Pi cannot determine the phone's internal encoder implementation. |
| CarPlay transport | JVM receives/authenticates/decrypts compressed media and forwards it over loopback; no server-side video decode/transcode. |
| CarPlay H.264 decode | CPU, GStreamer's `avdec_h264`, two slice threads. Frame threading is avoided to keep decode latency low. |
| CarPlay colour conversion and scaling | **GPU**, planar I420 textures and an OpenGL shader, with bilinear scaling. Confirmed on the actual V3D driver. |
| Android Auto H.264 decode | CPU. The installed OpenAuto library logged selection of `avdec_h264`; its preferred hardware factories are absent on this Pi. |
| Android Auto rendering | Existing QtGStreamer Quick video surface; not replaced in this change. |
| Music | Compressed AAC/Opus or PCM forwarded to native GStreamer playback; software decoding and volume, then system audio output. No media encoding. |
| Compatibility microphone | Pre-encoded Opus silence or zero PCM, timed and encrypted. No live audio encoder or capture device. |
| Camera | Existing V4L2/RTSP, decodebin, optional SVG overlay and QtGStreamer Quick surface. Decoder depends on the camera's negotiated format; hardware acceleration is not guaranteed. |

The Pi 5 has a hardware **HEVC** decoder, not an H.264 decoder. Switching to a
nonexistent H.264 hardware factory would break playback. The current working
native session negotiates H.264; the installed GStreamer registry did not expose
a usable hardware H.264 or HEVC decoder. HEVC would require a separate negotiated
codec/driver integration and hardware acceptance test.

Primary hardware references: [BCM2712 documentation](https://www.raspberrypi.com/documentation/computers/processors.html),
[Pi 5 product brief](https://datasheets.raspberrypi.com/rpi5/raspberry-pi-5-product-brief.pdf).

## Changes

Previously, every decoded frame went through I420 → BGRx conversion, a full RGB
image copy, and QPainter scaling. Now the normal display path is:

```text
encrypted H.264 → receiver decrypts → loopback WebSocket
  → shared compressed packet storage → h264parse → avdec_h264
  → retained, mapped I420 decoder buffer → GPU texture upload
  → GPU colour conversion + scaling → Qt composition → Wayland display
```

- The video buffer remains referenced until its readers release it. No decoded
  RGB image is generated or copied on the normal GPU path.
- I420 carries 1.5 bytes/pixel, versus 4 for BGRx, before row padding. This reduces
  the pixel payload by 62.5%; it is **not** a claim of a fully zero-copy pipeline.
  CPU-to-GPU texture uploads still occur.
- Textures and vertex storage are reused. Unchanged frames are not uploaded
  again, and hidden surfaces do not continuously repaint.
- Video and audio packets retain QByteArray's shared storage directly in
  GstBuffer, removing the payload slice and subsequent memcpy.
- The renderer handles padded strides and negotiated colour matrix/range. It
  preserves aspect ratio and the existing touch coordinate mapping.
- A lazy RGB snapshot supports headless tests and software rendering. Set
  `CARPLAY_RENDERER=software` before launching Dash to force that fallback.
  Initialization/shader failure also selects the software renderer.
- Compressed reference frames are preserved. Existing bounded latest-frame
  display, overload keyframe recovery, audio buffering and audio routing remain.

No codec quality, resolution or frame rate was reduced. No overclocking,
governor change or thermal protection change was made. No speculative changes
were made to the already-working audio clocking or cryptography.

## Reproducible before/after measurements

`carplay-media-benchmark` generates 300 deterministic H.264 moving-ball frames
**before** timing. It then replays them at 30 fps through a real loopback
WebSocket, the actual native decoder, and a visible 1920×1080 surface. Production
Dash was stopped to isolate the benchmark. Both benchmark binaries used the same
native-test CMake configuration (no explicit build type), libraries and fixture
parameters. These are not whole-application Release-build CPU measurements.

| Input | Frames, before/after | CPU, before → after | Frame preparation p50, before → after | p95, before → after |
| --- | --- | --- | --- | --- |
| 800×480 | 300/300 → 300/300 | 27.3% → 31.1% | 2.60 → 0.82 ms | 15.62 → 1.11 ms |
| 1280×720 | 300/300 → 300/300 | 38.8% → 35.1% | 5.89 → 1.51 ms | 6.02 → 1.66 ms |
| 1920×1080 | 300/300 → 300/300 | 63.7% → 46.3% | 12.60 → 3.25 ms | 16.31 → 3.50 ms |

CPU percentages refer to **one CPU core** (100% = one core), not the Pi's total
four-core capacity. CPU covers the benchmark process, not the compositor or GPU.
Frame preparation runs from local packet send until the GUI receives the decoded
frame notification. It excludes GPU completion, display scanout and phone/network
latency; it must not be presented as end-to-end CarPlay latency.

At 1080p this run used about **27% less process CPU** and prepared frames about
**3.9× sooner**. At 800×480 the GPU path traded approximately 3.9 percentage points
of one core for lower preparation latency and smoother scaling. It is not a CPU
improvement at every resolution. Results are single short synthetic runs, not a
promise of all-content or sustained 60 fps performance.

Raw samples are in `performance-results/`. Reproduce the candidate with:

```sh
cmake -S carplay/native-tests -B build/carplay-native-tests
cmake --build build/carplay-native-tests --target carplay-media-benchmark -j2
QT_QPA_PLATFORM=wayland build/carplay-native-tests/carplay-media-benchmark 1920 1080 300
```

Set `XDG_RUNTIME_DIR` and `WAYLAND_DISPLAY` for the active local Wayland session
when launching remotely. The benchmark displays its own window.

For graphics QA, set `CARPLAY_BENCHMARK_PATTERN=smpte` and
`CARPLAY_BENCHMARK_SCREENSHOT=/tmp/carplay-colours.png`. The benchmark compares
the GPU screenshot with GStreamer's software conversion of the **same** final
decoded frame. Padded 162×98 and 1920×1080 tests passed with mean channel errors
of 2.86 and 3.10 out of 255, including resampling/chroma-edge differences.

## Acceptance and remaining limits

- Native suites passed on Mac and Pi, including a retained-buffer lifetime and
  padded-stride colour test, actual H.264 decoding of 60 dependent frames, input
  routing, connection lifecycle and Dash configuration shutdown.
- The complete Release Dash application compiled on the Pi and reconnected the
  real iPhone using the V3D OpenGL YUV renderer.
- The user confirmed scrolling/apps, picture, touch, music and the OpenDash
  return screen all work. Live counters showed received/decoded frame counts
  matching and zero queued compressed bytes during the interaction.
- The user also confirmed the USB webcam and returning to CarPlay both remain
  normal. After a subsequent Pi restart, the optimized app was reopened and the
  native tests passed again. Saved real-phone settings are 1280×720 at 60 fps;
  this functional check is separate from the controlled 30 fps benchmark.
- The Pi reached 84.5°C and reported active frequency capping/throttling during
  compilation/testing. Cooling remains a limit on sustained maximum clocks.
- Long sessions, sustained 60 fps/high-detail content and higher-resolution
  real-phone tests remain separate. Short 30 fps fixtures do not certify them.
- No Android Auto performance improvement is claimed: its installed external
  OpenAuto media library was inspected but not patched or rebuilt here.
