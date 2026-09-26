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
Use `:quit` or Ctrl+] to return to `user@teensy41:/`. The initial SD bridge is
read-only: saved history, persistent settings and calculator `:save` are not
available. The original `test_teensy41_serial.py` remains for recovery firmware.

The host suite also renders the real calculator's ANSI output at five terminal
heights, covering typing, backspace, arrow keys, results and scrolling. This
catches cursor placement errors that a text-only USB transcript cannot detect.
