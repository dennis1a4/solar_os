# Teensy hardware checks

Run commands from the SolarOS repository root. The hardware script requires
Python 3 and `pyserial`, the baseline firmware with `sdinfo`, and exclusive use
of the USB console: close other serial monitors first. It discovers a single
Teensy USB serial device automatically; use `--port` if needed. Linux's ttyACM
number can change after unplugging/reflashing.

The script only issues console status, calculation, mount, listing and optional
file-read commands, plus an optional PSRAM allocation/write/read/free check.
It does not write SD files or added flash. Logs include directory listings
and the content of any file explicitly selected with `--read`.

## Cold start with a card

Unplug USB for five seconds with the SD card inserted, then reconnect. Run:

```sh
python3 scripts/ports/test_teensy41_serial.py --expect mounted --repeat 5 \
  --log /tmp/teensy-cold.json
```

Do not add `--mount` for this check: automatic startup mounting is what is being
tested. Optionally add `--read /path/to/existing-small-text-file.txt` to compare
repeated file reads as well as directory listings. No particular file is required.

## Missing card and later insertion

Unplug USB, remove the card, and reconnect. Run:

```sh
python3 scripts/ports/test_teensy41_serial.py --expect absent --mount --repeat 5 \
  --log /tmp/teensy-absent.json
```

This checks the initial mount diagnostics, explicit mount failure, continued
console/calculator operation and heartbeat progress. Allow several seconds for
the bounded SD retries.

Then insert the card while the idle board remains powered and run:

```sh
python3 scripts/ports/test_teensy41_serial.py --expect mounted --mount --repeat 5 \
  --log /tmp/teensy-inserted.json
```

This tests insertion after a failed mount, not removal/replacement of an already
mounted card. The latter remains unsupported by the bootstrap storage adapter.

## Warm start and stability

After a normal baseline upload/restart with the card inserted, repeat the
cold-start command without power cycling. Use a separate log for each run.
Increase `--repeat` to 1000 for a longer sequence of calculator, root-listing,
and optional text-file reads. The script checks stable output and reported free
heap, positive console stack headroom, and advancing uptime/heartbeat. It is
not a full RAM test, filesystem integrity check, or exhaustive leak detector.

Host regression tests remain available with `bash scripts/ports/test_teensy41.sh`.

## Fitted PSRAM

After completing the unchanged USB comparison workload, check installed PSRAM:

```sh
python3 scripts/ports/test_teensy41_serial.py --expect mounted --psram \
  --repeat 100 --log /tmp/teensy-psram.json
```

This requires the firmware's cache-flushed 4 KiB PSRAM check to pass before
and during the loop. The `mem` responses in the JSON transcript include the
PSRAM capacity detected at boot. Repeated small allocations do not test the
whole chip or every address line. Added QSPI flash is not probed or tested by
this script; the current bootstrap firmware has no added-flash command.

## Upstream USB shell target

For `teensy41_shell` firmware, use the separate test client:

```sh
python3 scripts/ports/test_teensy41_shell.py --read /test.txt --repeat 1000 \
  --log /tmp/teensy-upstream-shell.json
```

Omit `--read` to repeat directory listings instead of reading a file. The test
requires a mounted card; it does not create files. It checks the real shell,
command parsing, editing, Ctrl-C, history, Tab completion, calculator evaluation,
interactive calculator launch and return via Ctrl+] / `:quit`, missing paths,
SD reads, stable internal/external free memory, uptime and stack headroom.
Logs include selected file contents. Close other serial monitors first.

For interactive use, open a VT100/ANSI serial terminal at 115200 with 80×24
geometry. In Konsole use `pio device monitor --baud 115200 --raw --exit-char 28`:
`--raw` preserves ANSI controls, and Ctrl+\ exits the monitor so Ctrl-C can
reach the shell. `calc -e "2 + 3 * 4"` returns 14; `calc` enters the calculator.
Use `:quit` or Ctrl+] to return to `user@teensy41:/`. The SD bridge now supports writes; persistent configuration remains unsupported.
See the port notes for editor/Python usage and `setterm size COLS ROWS`. The original `test_teensy41_serial.py` remains for recovery firmware.

The host suite also renders the real calculator's ANSI output at five terminal
heights, covering typing, backspace, arrow keys, results and scrolling. This
catches cursor placement errors that a text-only USB transcript cannot detect.

## Writable SD, editor and MicroPython

With the `teensy41_shell` firmware, a mounted SD card and fitted PSRAM:

```sh
python3 scripts/ports/test_teensy41_writable.py --repeat 50 \
  --log /tmp/teensy-writable.json
```

Close the serial monitor first. This test creates a unique
`/_solaros_test_<random>` directory and retains its files for inspection. It
exercises real editor saves/replacement and dirty-exit prompts, copy/move/delete,
file modes and seek/flush, Python REPL/scripts/imports, PSRAM allocation and GC,
interrupts, exceptions, allocation/descriptor exhaustion, recovery-file
protection, terminal geometry, and repeated application cleanup. Normal shell
history writes also occur. It does not test surprise card removal, power loss,
a full SD card, or the entire PSRAM capacity. Logs capture only test output;
the separate shell read test may include selected personal file contents.

To check persistence, substitute the directory reported by your successful run:

```sh
python3 scripts/ports/test_teensy41_writable.py \
  --verify-existing /_solaros_test_111186ef8a --reboot \
  --log /tmp/teensy-writable-reboot.json
```

This intentionally restarts the board, waits for USB to settle, and checks the
saved file contents and Python execution. It is a software restart, not a
physical power-cycle or interrupted-write test.

## Rev D shield audio

Generate quiet original test files and test the real decoder/SD transport on
the host (requires FFmpeg and a C compiler):

```sh
bash scripts/ports/test_teensy41_audio_host.sh
```

The script prints its retained `/tmp/solaros-audio.XXXXXX` fixture directory.
It uses address/undefined-behavior sanitizers; LeakSanitizer needs an environment
without ptrace. After confirming wiring and uploading `teensy41_audio`, close
other serial monitors and substitute that fixture directory:

```sh
python3 scripts/ports/test_teensy41_audio.py \
  --fixtures /tmp/solaros-audio.XXXXXX --repeat 20 \
  --log /tmp/teensy-audio.json
```

This test emits quiet tones, copies only generated audio to a new
`/_solaros_audio_<random>` directory through the board's Python REPL, verifies
SHA-256, then tests stereo 44.1 kHz MP3, mono 48 kHz MP3, mono 22.05 kHz WAV,
playback duration, block/underrun counters, cancellation, input errors and
repeated app cleanup. Test files remain on SD. Someone must also listen and
confirm both channels sound correct; software counters cannot prove analog
output quality. This suite passed 20 playback cycles on the wired Rev D shield
on 2026-09-26; see the port notes for the exact evidence and remaining limits.

For microphone capture, connect an external mic to MIC/GND and Teensy pin 8
to shield DOUT, with the speaker near the mic. Close the serial monitor. The
following test needs NumPy and pyserial on the host:

```sh
python3 scripts/ports/test_teensy41_mic.py \
  --log /tmp/teensy-mic.json --wav /tmp/teensy-mic.wav
```

It records four seconds around a 440 Hz speaker tone, checks the WAV format,
cancellation header, overwrite protection and capture overruns, and requires
a clear rise/fall in tone energy. It reports clipping and spectral measurements;
a pass does not establish clean analog quality. Uniquely named WAVs remain on
SD. The retrieved host WAV contains microphone audio, potentially including
nearby conversation; keep it and the logs outside the repository.
