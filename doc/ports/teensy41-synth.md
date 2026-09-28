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
foreground worker stack reservation; the app declares an 8 KiB stack requirement.

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
