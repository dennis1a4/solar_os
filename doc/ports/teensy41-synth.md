# Teensy 4.1 Synth

The `teensy41_synth` profile extends `teensy41_apps` with a terminal instrument,
reusing the shared eight-voice SolarOS synthesizer and DSP engine. It uses the
already wired Rev D SGTL5000 audio shield, stereo I2S output at 44,100 Hz, and
headphone volume initially set to 20 percent. No additional wiring is required.

## Playing

Run `synth` (or `synth --headless`) from the USB terminal.

| Keys | Action |
|---|---|
| `a w s e d f t g y h u j k` | Chromatic C4 through C5 at the default octave |
| `H` | Toggle hold; press each note again to release it |
| `Space` | Release all notes using the release envelope |
| `1`–`5` | Square, triangle, saw, sine, noise |
| `z` / `x` | Lower / raise octave, range 2–6 |
| `-` / `+` | Lower / raise headphone volume by 5 percent |
| `[` / `]` | Select attack, decay, sustain, release, cutoff, resonance, oscillator 2 mix |
| `,` / `.` | Decrease / increase selected parameter |
| `?` | Show controls, voice count and audio performance counters |
| `q`, Ctrl-C or Ctrl-] | Stop synth and return to the shell |

A normal serial terminal does not transmit key-up events. Notes therefore pulse
for 220 ms unless hold is enabled. Hold is a toggle, not physical key-release
tracking; keyboard auto-repeat can toggle a held note repeatedly. Changing
octave or hold mode releases existing notes. Up to eight notes play at once;
the shared engine steals voices when that limit is exceeded.

ADSR times are milliseconds; sustain, resonance and oscillator mix are
percentages. The second oscillator initially uses square wave at the same pitch.
`peak` and `hash` describe the last captured active PCM block and remain latched
after silence. They are generation diagnostics, not a measurement of analog
headphone output.

This first port provides terminal controls. The upstream graphical editor,
physical keyboard key-up integration, MIDI, preset file controls, custom-wave
editor, control bindings and Python/Lua synth APIs are not enabled by this
profile yet. Sound parameters stay in the engine across app restarts, but are
not saved across reboot; the app starts with sine wave and volume 20 percent.

## Architecture

`solar_os_synth_voice.c` and `solar_os_dsp.c` are shared unchanged. The port's
`synth_backend.cpp` implements the synth service on the existing foreground
worker, and `synth_app.c` supplies a terminal app under the usual `synth` registry
entry. The native graphical app remains unchanged for other platforms.

App state allocates from PSRAM and is released on exit. The small voice engine,
synchronization state and worker PCM scratch use fixed internal storage. Code
and constants run from cached program flash. The worker uses the existing
foreground worker stack allocated on demand; the app declares an 8 KiB stack requirement.

The synth output producer never reads terminal input. It caps queued audio at
four 128-frame blocks (about 12 ms), plus downstream I2S buffering and render/input
latency. This is not a measured end-to-end latency guarantee. Playback and synth
share the single foreground app slot. Exiting cancels blocked writes, clears
queued output and waits for the worker to stop before its stack is reused. USB
terminal disconnection stops the audio worker; reconnecting closes the app.

## Build and checks

```sh
PLATFORMIO_BUILD_DIR=/tmp/solaros-synth-build pio run -e teensy41_synth
bash scripts/ports/test_teensy41_synth_host.sh
PLATFORMIO_BUILD_DIR=/tmp/solaros-synth-build pio run -e teensy41_synth -t upload
python3 scripts/ports/test_teensy41_synth.py --log /tmp/teensy-synth.json
python3 scripts/ports/test_teensy41_synth.py --disconnect-only --log /tmp/teensy-synth-disconnect.json
```

Close other serial monitors before upload or hardware tests. Hardware tests play
notes at 10 percent headphone volume and exercise eight-voice load, oscillator
mix, filtering, release, cancellation and repeated app cleanup. They check PCM
and output telemetry; headphone listening is a separate check.

## Validated on 2026-09-27

The profile was built, uploaded and exercised on the fitted Teensy. Image size:
861,532 bytes flash, 426,944 bytes RAM1 and 184,720 bytes RAM2. Compared with the
previous apps profile, this adds 13,136 bytes flash and 5,952 bytes internal RAM.
The previous profile also rebuilds at its original sizes.

- ASAN/UBSAN host tests passed all six engine waveforms, stereo sample agreement,
  eight voices, both oscillators, filtering, ownership rejection, voice stealing,
  exact silent PCM after release, invalid arguments and restart.
- On-device tests passed all five exposed waveforms, terminal pulse and hold,
  eight voices, live filter/oscillator changes, and 20 exits/restarts with stable
  memory: 40,884 / 89,120 bytes internal heap free and 8,385,240 / 8,388,608 PSRAM.
- Eight simultaneous voices with oscillator mix 50 percent, cutoff 8 kHz and
  resonance 40 percent produced no render deadline misses, output underruns or
  write errors. Maximum observed render time was 2,808 us per 256-frame block,
  against a 5,805 us budget. This is the tested configuration, not an exhaustive
  guarantee across every possible patch or background load.
- Closing USB with held notes stopped output. Reopening returned to the shell;
  subsequent synth playback and Ctrl-C exit passed.
- Existing audio host tests passed real MP3/WAV decoding, resampling, cancellation
  and allocation cleanup. Hardware MP3/WAV playback, cancellation, missing-file
  handling and three playback cycles passed on the new firmware.

Logs: `/tmp/teensy-synth.json`, `/tmp/teensy-synth-disconnect.json`,
`/tmp/teensy-synth-audio.json`, `/tmp/teensy-synth-audio-host.log`.
Firmware and logs are retained under `solar_os-baselines/2026-09-27-synth`
alongside the repository. Listening at the headphones has not been checked during
this session; the user was away from the computer.

## On-demand audio rings — 2026-10-05

The shared playback ring (16 KiB) and microphone capture ring (32 KiB) now
allocate only on start, using internal-preferred memory (OCRAM first, guarded
DTCM fallback). Allocation failure returns NO_MEM. The AudioStream interrupt
is disabled while publishing/detaching pointers and resetting queue state;
allocation/free happens outside that interrupt-disabled section. The producer
must finish before cleanup, as enforced by the existing audio app ownership and
worker stop/join paths. Audio library blocks and DMA storage remain unchanged.

`audio status` shows allocated playback/capture ring bytes. Normal completion,
cancellation and error cleanup return the rings. Tones/clock alarms generate
samples directly into AudioStream blocks and do not allocate either ring.
Diagnostic tone/off/mictest commands refuse while an audio app is active or
retained on any console; microphone diagnostics also block competing audio app
launches while their synchronous capture operation yields to other consoles.

At 44.1 kHz, the playback ring's
usable 31 stereo blocks cover about 90 ms; 127 mono capture blocks cover about
369 ms. SD recordings now add the independently fed PSRAM queue below.
Current WAV recording streams to storage at 88,200 bytes/s and retains the
one-hour limit (about 318 MB plus header).

Host checks: `python3 tests/ports/test_teensy41_audio_memory.py` covers actual
ring/ISR code, 100 lifecycle cycles, allocation/configuration failure, failed
restart, drain/cancellation/timeout, capture overflow, sample integrity, and
interrupt injection at pointer handoff. It also checks actual cross-console
app admission for active/retained/remote ownership. ASan/UBSan and existing
MP3/WAV/resampling/cancellation host tests pass. The device procedure is
`scripts/ports/test_teensy41_audio_memory.py --log /tmp/teensy-audio-rings-device.json`;
it removes its unique SD fixtures after success and distinguishes missing-codec
checks from live recording/playback. Analog sound and loaded audio coexistence
remain hardware acceptance items when the codec is reconnected.

## Concurrent diagnostic capture

`audio monitor NEWFILE.wav` captures four seconds of microphone input while
leaving existing playback running. Use a second console for WebRadio/Synth
(e.g. launch playback on the LCD, then issue monitor on USB). It refuses an
active or retained recorder and never overwrites a file. Microphone capture
blocks competing audio app starts until completion; tone/off/mictest remain
blocked while a playback app owns audio. Capture allocation and codec failures
leave playback intact. Stop with Ctrl+C; the partial WAV is finalized.

For short acoustic checks, a temporary RAMFS avoids storage writes during
capture; a 512 KiB mount has room for the 352,844-byte four-second mono WAV.
Copy the recording to persistent storage after playback/capture stops. This
is a bounded diagnostic; general continuous full-duplex recording is not exposed.


## Longer SD recording — 2026-10-05

Use `arecord -d 300 /sd/recording.wav` for five minutes, or omit `-d` and
stop with Ctrl+C, Esc or Ctrl+]. Existing files are refused. The one-hour
limit remains; storage space limits recording length independently of RAM.

Paths under `/sd/` allocate a 256 KiB PSRAM single-producer/single-consumer
queue, in addition to the existing 32 KiB internal capture ring and 4 KiB
PSRAM write buffer. An on-demand priority-3 task with a 2 KiB internal stack
copies blocks from the ISR's internal ring into PSRAM every RTOS tick. It
never uses the filesystem, console gate, codec or allocator. Foreground
writes aggregate up to 4 KiB; the feeder keeps running when those writes
stall. The PSRAM queue holds 1,023 blocks (about 2.97 seconds), followed by
another 127 internal blocks (0.37 seconds). Sustained overflow is an error,
not silent overwriting. No DMA or ISR buffers move to PSRAM.

Stop disables capture, joins the suspended feeder, and returns both queues
and the stack before finalizing/syncing the WAV. Cancellation saves the data
already handed to the writer; unwritten queued tail samples are discarded.
Allocation/task admission failures roll back all resources. `audio status`
reports allocated queue bytes and the last peak occupancy and feeder stack
headroom. Routine history flash saves defer until capture stops; explicit
flash writes from another console should still be avoided, since flash
programming masks AudioStream interrupts. Flash/USB recording paths retain
only the original short internal queue; long flash recording is out of scope.

Host tests inject repeated 512-block (~1.49-second) writer stalls and queue
wraps, verify every sample, and cover allocation/task failures, bounded
queue overflow, cancellation and cleanup. WAV tests cover capture failure,
final sync failure, allocation failure and partial header finalization.

Live acceptance on the installed legacy bench image: 4/30/180/300-second SD
recordings passed exact length/header and elapsed-time checks with zero
overruns. The five-minute file contained 26,460,000 PCM bytes and completed
in 300.222 seconds. Maximum observed spool occupancy across runs was 44,800
bytes; feeder stack headroom was 1,940 bytes. Cancellation, existing-file
protection, invalid destinations and exact idle memory recovery also passed.
The full one-hour limit and arbitrary SD cards/loads have not been tested.
