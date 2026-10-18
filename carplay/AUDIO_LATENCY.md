# CarPlay audio responsiveness — 18 October 2026

This concerns the iPhone's **wireless CarPlay audio over Wi-Fi**, played through
the Pi's HDMI output. It does not tune ordinary Bluetooth A2DP audio.

## Findings and changes

1. The selected GStreamer PulseAudio output had a default 200 ms ring-buffer
   request. Dash now requests **60 ms**, retaining its 10 ms write period and
   clock synchronization. The real output is configured when `autoaudiosink`
   creates its Pulse/ALSA/CoreAudio child, before preparing that buffer.
2. The receiver acknowledged RTSP **FLUSH** without forwarding it to playback.
   Pause/seek/track changes could therefore leave queued sound playing for
   nearly a second. FLUSH now resets playback queues for that session's audio
   streams. Each reset is serialized with RTP delivery, after an in-flight
   callback and before the next callback. The phone's `RTP-Info` cutoff also
   rejects authenticated pre-flush UDP packets, including old packets arriving
   after new audio. The cutoff remains for three seconds of the new RTP timeline
   and handles timestamp wrap; sequence-only headers use a bounded wrap-aware
   fallback. Auxiliary speech/navigation timelines are left alone.
3. Existing wire messages are reused: AUDIO_STOPPED followed by the cached
   AUDIO_CONFIG. Native and browser clients already clear queued playback and
   rebuild the decoder on those messages. The audio UDP transport, format,
   video session, authentication and silent microphone stay alive.

The 200 ms GStreamer **queue capacity** is retained for network/decoder bursts;
that capacity is not an intentional fixed playback delay. No global PipeWire
settings, Bluetooth codecs, audio sample rates or compression quality changed.

## Observed local output negotiation

PipeWire 1.2.7, 48 kHz, stereo float stream → HDMI:

| Property | Before | With 60 ms sink request |
| --- | --- | --- |
| Stream `node.latency` request | 4320/48000 = 90 ms | 960/48000 = 20 ms |
| Pulse target buffer (`tlength`, 8 bytes/frame) | 42240 bytes = 110 ms | 15360 bytes = 40 ms |
| HDMI graph quantum | 2048/48000 = 42.67 ms | 512/48000 = 10.67 ms |

These are negotiated/requested buffer quantities, not end-to-end acoustic
latency. They should not be summed as independent delays. The graph changed
automatically in response to the stream request; its quantum was not forced.

## Pause measurement and flush acceptance

The test sent the normal media Pause command through the authenticated local
receiver WebSocket, then measured RMS levels from **HDMI's digital monitor**.
Only RMS/timing numbers were retained, not audio recordings. The capture used
48 kHz S16 stereo, a requested 10 ms capture latency and 10 ms analysis blocks.
First quiet is the first of five consecutive blocks below 2% of recent music RMS
(with a minimum threshold). Settled quiet is the block after the last audible
block in the three-second observation window, so a returning stale tail is counted. The log's FLUSH receipt was watched concurrently.
Playback was resumed between trials and after the test.

Both comparisons below used the **same 60 ms output-buffer setting**, isolating
the effect of implementing FLUSH:

| Trial | FLUSH ignored: first quiet | Queue reset only: first quiet |
| --- | --- | --- |
| 1 | 937.0 ms | 82.0 ms |
| 2 | 944.8 ms | 79.6 ms |

The initial first-quiet detector missed a stale UDP tail: the user heard sound
stop, briefly return, then stop for good. A follow-up measurement reproduced
that bug, with first quiet at 64.2–67.9 ms but **settled quiet at 927.1–929.3 ms**.
Those initial 80 ms figures therefore do not establish a complete pause response.
With the RTP cutoff enforced:

| Trial | Pause → first quiet | Pause → settled quiet | Audible return after first quiet |
| --- | --- | --- | --- |
| 1 | 73.6 ms | 73.6 ms | 0 ms |
| 2 | 88.3 ms | 88.3 ms | 0 ms |

No audible return was observed during either three-second window. The live log
confirmed pre-flush packets being discarded. The user then confirmed that Pause
stops cleanly and that Resume, track changes, lyrics and playback are good.

This includes local input dispatch, phone response, playback clearing and monitor
capture latency. It excludes the touchscreen input stage and any downstream HDMI
display/speaker processing. These short two-trial measurements do not certify
all Wi-Fi environments, tracks, lyric synchronization or physical speaker delay.
Raw timing and negotiated-property samples are in `performance-results/`.

## Verification and fallback

- Mac/Pi native tests pass, including automatic late-created sink configuration,
  validated buffer overrides and retaining clock synchronization.
- JVM protocol tests: 103 passed; server tests: 27 passed, one external identity
  test skipped. Tests cover FLUSH routing/session isolation, ordering against a
  blocked RTP callback, continued authenticated UDP delivery, retained transport
  negotiation, malformed cutoff headers, stale packets both before and after newer
  audio, timestamp/sequence wrap and cutoff expiry, and STOPPED → CONFIG → next audio packet ordering.
- Actual phone FLUSH requests now log `airplay audio playback flushed` and
  recreate the native output with `bufferUs=60000 periodUs=10000`.
- The initial smaller-buffer trial was reported to have fewer clicks/dropouts,
  but still approximately one second of delay; the measurements then identified
  and corrected the missing FLUSH path and stale packet handling.

For output devices or networks needing a larger safety margin, set
`CARPLAY_AUDIO_BUFFER_MS=200` in Dash's launch environment. Valid overrides are
40–500 ms; missing/invalid values select 60 ms. This restores the previous buffer
request while keeping correct FLUSH behavior. Lower values are not automatically
better: Wi-Fi delivery and device scheduling still require some headroom.
