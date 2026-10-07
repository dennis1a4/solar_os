# Teensy hardware checks

See the [Teensy quick-start](../../doc/ports/README.md) for the current profiles
and wiring. The sections below also describe earlier bring-up profiles.

Current installed firmware: `teensy41_telnet_legacy` (AmpEn40). The normal display
profile requires AmpEn0/ADC40 rewiring. See the [handover](../../doc/ports/teensy41-handoff.md)
and [master checklist](../../doc/ports/teensy41-test-checklist.md) before hardware tests.

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

## Native Ethernet

After connecting the PJRC kit to a DHCP LAN and uploading `teensy41_network`,
close the serial monitor and run:

```sh
python3 scripts/ports/test_teensy41_network.py --log /tmp/teensy-network.json
```

This checks link, DHCP, invalid arguments and a software down/up cycle, leaving
Ethernet enabled. Optional `--resolve HOST` and `--connect HOST PORT` check
one explicitly chosen DNS name/TCP endpoint; there is no LAN scan. Physical
cable removal/reinsertion and networking alongside long audio/Python workloads
need separate hardware checks. Logs include the board's MAC and LAN addresses.

## MicroPython TCP clients

With `teensy41_network` uploaded and Ethernet connected, close the monitor:

```sh
python3 scripts/ports/test_teensy41_python_network.py \
  --board-ip 192.168.1.197 --log /tmp/teensy-python-network.json
```

Substitute the address shown by `network status`. The script starts a temporary
HTTP fixture server on the host LAN interface, transfers the example to a new
SD directory, and checks exact binary HTTP response bytes, exclusive-file
creation, four-socket limits, GC/interpreter cleanup, timeout/nonblocking
receive, Ctrl-C, repeated fetch memory stability and software network recovery.
It also resolves example.com; it does not scan the LAN. Successful test files
are removed unless `--keep-fixtures` is selected.

Use `--cable` for an interactive blocked-receive cable-removal/reconnection test.
Wait for its READY message before unplugging only Ethernet; reconnect when
prompted. The test checks a link-loss exception and an exact HTTP transfer over
a fresh socket afterward. Both actions have a 150-second test deadline.

Add `--public` to also fetch example.com over plain HTTP and copy the tested
example to `/http_fetch.py` if that destination is absent. Existing files are
protected by the shell copy command; inspect the logged copy result.


## Shared SolarOS network services

With the Ethernet firmware and DHCP LAN connected, close the serial monitor and run:

```sh
bash scripts/ports/test_teensy41_net_service_host.sh
python3 scripts/ports/test_teensy41_net_service.py --log /tmp/teensy-net-service.json
```

The host test compiles the real managed session service against a deterministic
transport with the undefined-behavior sanitizer. The hardware test uses the
existing `solaros.net` API, local TCP/UDP fixtures, shared interface/route commands,
timeouts/cancellation, quotas, stale handles, interpreter cleanup and network
restart. It creates no SD files. Use `--board-ip IP` if the board is on a different
LAN; this selects the host interface for the fixture servers, not a fixed board
address. No LAN scan is performed. Keep the standard Python socket/HTTP-to-SD
suite as a separate regression check.


## Added QSPI flash

The flash-enabled `teensy41_network` build exposes `flash status`, `mount`, `scan`
and blank-only `init`. Startup cannot format the chip. With the monitor closed:

```sh
bash scripts/ports/test_teensy41_flash_host.sh
python3 scripts/ports/test_teensy41_flash.py --probe --log /tmp/teensy-flash-probe.json
python3 scripts/ports/test_teensy41_flash.py --log /tmp/teensy-flash-files.json
```

The file suite requires a mounted filesystem, or explicit `--initialize-blank`
for a new chip. That option still refuses any nonblank media. It creates unique
`/sd/_solaros_flash_<id>` and `/flash/_solaros_flash_<id>` test directories, checks
65,806-byte binary transfers both ways, 20 PSRAM-buffer read/write/hash cycles,
file I/O at Python's recursion limit, file moves/modes, editor save/replacement,
Python execution, handle exhaustion and cleanup. Successful runs remove their
fixtures; use `--keep-fixtures` on the initial run for later persistence checks. It never overwrites unrelated paths. Optional `--audio-fixtures
/_solaros_audio_<id>` copies the previously generated tone fixtures onto flash
and plays them at 10% volume through the existing aplay app.

Use the printed directory name from a `--keep-fixtures` run to verify after a software reboot.
`--verify-existing` preserves the supplied directory because it was not created by that run:

```sh
python3 scripts/ports/test_teensy41_flash.py --verify-existing _solaros_flash_<id> \
  --reboot --log /tmp/teensy-flash-persistence.json
```

This does not replace a cold power-cycle or power-loss test. Existing SD tests
remain applicable because their root paths retain the SD interpretation.
Run the existing socket/HTTP suite with `--volume /flash` to execute the same
example from flash and verify downloads there:

```sh
python3 scripts/ports/test_teensy41_python_network.py --volume /flash \
  --log /tmp/teensy-flash-python-flash.json
```

## Teensy SSH client

`teensy41_ssh` extends Ethernet/flash with the upstream SSH client and pinned
libssh2/Mbed TLS. `scripts/ports/test_teensy41_ssh.py` requires Paramiko and
pyserial, starts a temporary server on one LAN address and a random port, and
uses disposable password credentials. It never executes commands on the host
or changes the system SSH service. SolarOS retains the fixture's public host
key in its normal `.ssh/known_hosts` file, scoped to the random port.

```sh
python3 -m venv --system-site-packages /tmp/solaros-ssh-testenv
/tmp/solaros-ssh-testenv/bin/pip install paramiko pyserial
/tmp/solaros-ssh-testenv/bin/python scripts/ports/test_teensy41_ssh.py \
  --log /tmp/teensy-ssh-full.json
```

The full test covers password auth, 4 KiB terminal output, input/backspace/Ctrl-C,
wrong-password and changed-host-key rejection, normal/abrupt disconnection,
Ctrl-] cancellation (including a silent handshake peer), repeated sessions,
and memory recovery. `--smoke` runs just the successful login/I/O/exit path.

### Teensy Files integration

Use the `teensy41_files` profile for the combined SSH and Files firmware.
The fixture needs `pyserial` and `pyte` in a host virtual environment:

```sh
python scripts/ports/test_teensy41_files.py --log /tmp/teensy-files.json
bash scripts/ports/test_teensy41_children_host.sh
```

The serial monitor must be closed. The device test creates unique
`_solaros_files_<random>` directories on SD and flash and removes them after success
(unless `--keep-fixtures` is selected). It drives
the real two-pane TUI through a terminal emulator, checks editor return, copy,
move in both directions, recursive copy, mkdir/delete, ZIP payloads, copy
cancellation and partial cleanup, failed Python child return, numeric sizes and
memory/handle cleanup. Use `files /` on the board; Q exits. Build/upload with
`PLATFORMIO_BUILD_DIR=/tmp/solaros-files-build pio run -e teensy41_files`
(add `-t upload` for upload).

### Teensy settings and text apps

`teensy41_apps` extends the combined Files profile with flash-backed preferences
and the shared `less`, `notes`, and `sheet` applications. Build separately:

```sh
PLATFORMIO_BUILD_DIR=/tmp/solaros-apps-build pio run -e teensy41_apps
bash scripts/ports/test_teensy41_settings_host.sh
python scripts/ports/test_teensy41_apps.py --log /tmp/teensy-apps.json
```

The hardware suite requires pyserial and pyte, a closed serial monitor, SD and
mounted flash. It tests saved identity/geometry/startup selection across reboot,
both storage volumes, pager search, Notes save/reopen, Sheet formulas, Files child
return, and repeated lifecycle/memory cleanup. Unique `_apps_<random>` fixtures
are removed after success unless `--keep-fixtures` is selected. Original preferences are restored. A temporary startup script is created
only if none exists, then removed; an existing script is preserved. If interrupted,
use `--restore-from <previous-log>` with a different `--log` path to recover the
original preferences before retrying. The test waits for the reboot acknowledgement
before dropping USB DTR. Serial tests must run one at a time.

Settings host tests inject sync/rename failure, validate corrupt/truncated data,
check staged writes, type/length/capacity bounds, read-only and stale handles,
and unchanged-value write suppression under ASAN/UBSAN. Sanitizers require
execution outside the restricted sandbox on this host.

## Synth and independent LCD/USB terminals

The `teensy41_synth` profile adds the terminal instrument. The
`teensy41_display` profile adds an Adafruit RA8875 terminal and USB host keyboard
while retaining a separate USB serial shell. See the
[synth controls and evidence](../../doc/ports/teensy41-synth.md) and
[display integration notes](../../doc/ports/teensy41-display.md).

```sh
bash scripts/ports/test_teensy41_synth_host.sh
bash scripts/ports/test_teensy41_lcd_host.sh
python3 scripts/ports/test_teensy41_synth.py --log /tmp/teensy-synth.json
python3 scripts/ports/test_teensy41_display.py --log /tmp/teensy-display.json
```

Run device tests separately, with the serial monitor closed. The synth suite
requires the connected audio shield and plays at 10% headphone volume. The
display suite requires the local operator to leave the keyboard idle: it injects
LCD input to check session isolation, app ownership, Python cancellation,
memory cleanup and USB reconnect. It leaves the LCD at a shell prompt. Add
`--status-only` to read keyboard/USB-host status without injecting local input.
Text-buffer checks do not replace a visual screen and physical-keyboard check.

## Plot and Playground

The display profile includes native RA8875 Plot graphics and the Playground
browser/installer. See [port notes](../../doc/ports/teensy41-plot-playground.md).

```sh
bash scripts/ports/test_teensy41_http_host.sh
bash scripts/ports/test_teensy41_settings_host.sh
python3 scripts/ports/test_teensy41_plot_playground.py --log /tmp/plot-playground.json
```

The device suite needs exclusive serial access and an idle local keyboard. It
sets the RTC from the host UTC clock, starts Ethernet, downloads the official
catalog, and installs/runs Hello Python. It removes its unique CSV fixture and
leaves the catalog/example installed. Use `--plot-only` to skip the network work.
The HTTP sanitizer test also checks the embedded certificates, date validation
and hostname rejection using the same pinned crypto configuration as firmware.
Settings tests cover legacy-snapshot migration, long source URLs and key erasure.

## RA8875 graphics and images

See [graphics notes](../../doc/ports/teensy41-graphics.md) for the image decoder
host test and `test_teensy41_graphics.py` hardware acceptance test. Both need
Pillow; the device test retains a unique SD demo folder for manual use.

## MQTT Explorer

`test_teensy41_mqtt_host.sh` runs the bounded model/incremental codec under
ASAN/UBSAN. `test_teensy41_mqtt.py --log /tmp/teensy-mqtt-fixture-final.json`
starts an isolated local broker and exercises the Teensy LCD app, capture and
logging. Host and board must have LAN connectivity. The script controls the
local keyboard session and retains a unique SD log. Optional `--live-only
--live-host BROKER_IP --live-auth FILE` connects read-only to a real broker and
leaves the explorer open; the credential file is never copied into test logs.
See [MQTT port notes](../../doc/ports/teensy41-mqtt-explorer.md).

USB drive mounting and file operations (new unique fixture folders only):

```sh
python3 scripts/ports/test_teensy41_usb_storage.py --log /tmp/teensy-usb-test.json
```

Use `--no-drive` to verify the empty-host case. See the
[USB storage port notes](../../doc/ports/teensy41-usb-storage.md).

SD/USB/keyboard reconnect runner: `test_teensy41_hotplug.py`. Default mode tests
software storage eject/remount; `--physical` waits for an interactive operator.
See [SD recovery and examples](../../doc/ports/teensy41-sd-recovery.md). Host-only
checks: `bash scripts/ports/test_teensy41_sd_recovery_host.sh` and
`python3 tests/ports/test_teensy41_hotplug.py`. SD recovery is installed; physical recovery checks remain pending.

OBD/CAN software tests (no attached board required):

```sh
bash scripts/ports/test_teensy41_obd_host.sh
```

Tests both the portable CAN/ISO-TP/OBD model and actual `obd` app lifecycle using
simulated ECUs and a host terminal stub, with address/undefined sanitizers.
See [OBD notes](../../doc/ports/teensy41-obd.md). The demo is installed with the clock image; CAN hardware is not validated.

Clock/timezone host checks: `bash scripts/ports/test_teensy41_clock_host.sh`.
Exercises the upstream Clock app, Teensy countdown adapter, RTC/local-time
conversion and flash settings persistence. See [clock notes](../../doc/ports/teensy41-clock.md).

Clock remote device checks (sets RTC from host UTC, saves Manitoba timezone,
and drives the LCD app through USB diagnostics):

```sh
python3 scripts/ports/test_teensy41_clock.py --reboot --log /tmp/teensy-clock-device.json
```

Keep the keyboard idle and close serial monitors. Requires mounted test SD and
USB media; checks their boot mounts without modifying their files. `--reboot`
verifies timezone/RTC retention across software reboot. Does not verify LCD
appearance, sound or battery-backed RTC retention across power loss.

USB-PD model and actual app lifecycle checks:
`bash scripts/ports/test_teensy41_power_host.sh`. Sanitized host simulation; no
board required. See [power notes](../../doc/ports/teensy41-power.md).

Scope model/app checks: `bash scripts/ports/test_teensy41_scope_host.sh`.
Produces a host SVG preview; does not exercise ADC/DMA hardware. See
[scope notes](../../doc/ports/teensy41-scope.md) before flashing the new pin map.

Telnet server host tests: `bash scripts/ports/test_teensy41_telnet_host.sh`.
Device tests: `python3 scripts/ports/test_teensy41_telnet.py --log /tmp/teensy-telnet-device.json`.
Uses USB control and Ethernet, a temporary password file and explicit network
up/down; requires idle shells. See [Telnet notes](../../doc/ports/teensy41-telnetd.md).

## Workstation command acceptance

`test_teensy41_workstation.py --log /tmp/workstation.json` exercises manual search/
paging, shell ticks and watch exits, diagnostics, fixed-session listings,
ZIP byte round trips, HTTP binary hashes, curl cancellation/console isolation
and repeated memory recovery. Requires an idle keyboard, exclusive USB access,
SD storage and Ethernet on the host LAN. Starts a temporary host HTTP listener
and retains a uniquely named SD fixture directory. Does not set the RTC.

Host manual selection: `python3 -m unittest discover -s tests/ports -p test_teensy41_manual.py`.
The Clock host runner also tests local datetime writes, DST gaps and RTC bounds.
The Telnet suite covers remote `watch`, manual paging and session listing.

## USB keyboard repeat

```sh
bash scripts/ports/test_teensy41_keyboard_host.sh
```

ASan/UBSan covers delay/rate, clock wrap, release, live modifiers, multiple holds,
rollover, disconnect, app boundaries and complete ANSI events under overflow.
The user confirmed basic physical repeat/release on the installed legacy image.
KEY-2 through KEY-5 retain broader physical checks; injection cannot verify them.
Read `lcd` over USB for repeat counters without sending local keystrokes.
See [keyboard notes](../../doc/ports/teensy41-keyboard.md).

## Retained application sessions

```sh
bash scripts/ports/test_teensy41_sessions_host.sh
python3 scripts/ports/test_teensy41_sessions.py --log /tmp/sessions.json
python3 scripts/ports/test_teensy41_sessions_graphics.py --log /tmp/sessions-graphics.json
python3 scripts/ports/test_teensy41_telnet.py --log /tmp/sessions-telnet.json
```

Run hardware scripts sequentially with exclusive USB and an idle keyboard.
Session acceptance creates a uniquely named SD editor fixture. The optional
`--cleanup-id ID` closes a known test session from an interrupted run; never
supply an unrelated user's app ID. Telnet uses its existing temporary credential
fixture and includes remote Ctrl+Z/fg and retained-app disconnect cleanup.
Physical chords and display readability remain separate tests.

### Background jobs and scheduling

`bash scripts/ports/test_teensy41_jobs_host.sh` runs the actual shared lifecycle
and Teensy runner with sanitizers, date-conversion checks, and both shared
scheduler execution modes. `python3 scripts/ports/test_teensy41_jobs.py --log
/tmp/teensy-jobs-device.json --reboot` tests an idle board over exclusive USB,
creates unique SD fixtures, removes its schedule entries and verifies persistence.
See [jobs notes](../../doc/ports/teensy41-jobs.md) for command limits.

### Detachable Python processes

`test_teensy41_process.py`, `test_teensy41_process_edges.py` and
`test_teensy41_process_graphics.py` accept `--log PATH` and require idle consoles
and exclusive USB. They cover real VM/worker lifecycle, detached logging with
Calc/tail, input preservation, cross-console reattachment, disconnect policy,
output limits, coexistence with ZIP, graphics and cleanup. The main script's
`--cleanup-only ID` stops a known test process left by an interrupted run.
See [process jobs](../../doc/ports/teensy41-process-jobs.md).

## Network diagnostics and time synchronization (stage 3)

```sh
bash scripts/ports/test_teensy41_netdiag_host.sh
python3 scripts/ports/test_teensy41_netdiag.py --log /tmp/teensy-netdiag-device.json
```

The host test covers bounded target/port parsing and NTP validation with sanitizers.
The device test uses local TCP/NTP fixtures and exclusive USB; keep the keyboard
idle. It sets the RTC to host UTC, preserves timezone, leaves Ethernet running,
and checks cancellation, memory recovery and LCD/USB cooperation. See
[feature notes](../../doc/ports/teensy41-network-diagnostics.md).

## Shared Tab completion

```sh
bash scripts/ports/test_teensy41_completion_host.sh
python3 scripts/ports/test_teensy41_completion.py --log /tmp/teensy-completion-device.json
```

The device suite needs exclusive USB and an idle keyboard. It creates a unique SD
fixture, exercises middle-of-line/quoted paths, retained session and Python job
IDs, bounded USB/LCD listings and memory recovery. It leaves the fixtures for
inspection. See [completion API and limits](../../doc/ports/teensy41-completion.md).

## Hardware ownership and serial terminals

```sh
bash scripts/ports/test_teensy41_hardware_host.sh
python3 scripts/ports/test_teensy41_hardware.py --log /tmp/teensy-hardware-device.json
# Only after installing RX34–TX35 jumper, with no other devices on these pins:
python3 scripts/ports/test_teensy41_hardware.py --loopback uart8 --log /tmp/teensy-hardware-device.json
```

The host suite compiles the real resource/bus adapters with ASan/UBSan and mocked
hardware. Device checks require exclusive USB and an idle keyboard; they open
UART7/8, use spare pins as inputs and claim slot1 CS. They check board reservation
rejection, USB/LCD exclusion, COM suspension/resume, disconnect cleanup,
completion and stable memory. No external I2C/SPI data is written. Optional
loopback checks actual raw UART and COM echo. See
[hardware notes](../../doc/ports/teensy41-hardware-resources.md).

## PSRAM RAMFS

```sh
bash scripts/ports/test_teensy41_ramfs_host.sh
python3 scripts/ports/test_teensy41_ramfs.py --log /tmp/teensy-ramfs-device.json
python3 scripts/ports/test_teensy41_ramfs.py --edges-only --reboot --log /tmp/teensy-ramfs-edges.json
```

Requires idle consoles/exclusive USB and no existing RAMFS mounts. Uses unique
volatile mounts plus temporary SD/flash files and removes its fixtures. Tests
file/app routing, busy worker handles, quotas, completion, accounting and cleanup.
`--edges-only` selects editor/multiple-mount/LCD/flash coverage. `--reboot` adds a
board restart to verify volatility. Host sanitizer tests compile the actual
shared backend. See [limits](../../doc/ports/teensy41-ramfs.md).

## Python syntax highlighting

```sh
bash scripts/ports/test_teensy41_syntax_host.sh
/tmp/solaros-ssh-testenv/bin/python scripts/ports/test_teensy41_syntax.py --log /tmp/teensy-syntax-device.json
```

The device script needs pyserial and pyte (the existing test venv supplies both),
exclusive USB and idle consoles. It creates/removes `/syntax-test` in RAMFS;
that mount name must be unused. Checks token colors, selection, multiline edits,
resume, shell color restoration, LCD text and stable editor cleanup. See
[syntax limits](../../doc/ports/teensy41-syntax.md).

### Flash command history

`bash scripts/ports/test_teensy41_history_host.sh` checks batching, bounded
history, console isolation and atomic-save failure behavior with ASan/UBSan.
`python3 scripts/ports/test_teensy41_history.py --log /tmp/history.json` requires
idle USB and LCD consoles, adds harmless echo commands, waits one save interval,
reboots once and runs a shutdown dry-run without cutting power.

`bash scripts/ports/test_teensy41_entropy_host.sh` checks the actual entropy RPC
case with simulated peripheral states, including preservation of a ready sample
whose oscillator has stopped. The SSH device test accepts `--debug-log PATH`
for test-server negotiation diagnostics.

### Test fixture cleanup

Flash, Files, text-app, USB-storage and Python-network suites now remove only
their exact generated fixture roots after successful validation. `--keep-fixtures`
retains successful runs for inspection; failed/interrupted runs retain evidence.
The flash suite's `--verify-existing` always preserves the supplied fixtures.
No suite sweeps older directories or removes files outside its own generated roots.
Logs record removed/retained paths, and a cleanup failure fails the run.

`python3 tests/ports/test_teensy41_fixture_cleanup.py` verifies whole-batch path
validation, protected-path/traversal/injection rejection, retention, and failure
handling. The cleanup helper confirms each directory is absent after removal.

### FAT32 USB usage and batched reads

```sh
bash scripts/ports/test_teensy41_usb_batch_host.sh
python3 scripts/ports/test_teensy41_df.py --log /tmp/teensy-df-device.json
```

The host test checks the actual batching helper under ASan/UBSan, including
bounds, data/order, tails and failures. The device test requires mounted FAT32,
exclusive USB serial and idle consoles. It measures cold/warm/refresh timings,
compares maintained counts with recounts after file changes, hashes a temporary
513 KiB file through Python reads, checks refresh refusal with a suspended Python
reader, and logically ejects/remounts. Successful runs remove their unique USB
fixture; failures retain evidence. It never formats the drive. The first cold
`df` can include SD accounting too; later remount timing isolates USB with SD warm.

### Embedded Teensy manual coverage

```sh
python3 tests/ports/test_teensy41_manual.py
python3 tests/ports/test_teensy41_less_navigation.py
python3 tests/test_generate_manual.py
/tmp/solaros-ssh-testenv/bin/python scripts/ports/test_teensy41_manual_device.py --log /tmp/teensy-manual-device.json
```

Host checks compare page availability with the actual app/command registry gates
for full and reduced Teensy profiles. They verify complete body embedding,
Teensy hardware/storage contracts, absence of stale ESP instructions, and the
unchanged default upstream download policy. The device test requires pyserial,
pyte, exclusive USB serial, and idle LCD/USB consoles. It opens every installed
app/command page, checks both ends of the text, exits each pager, and exercises
bare aliases, search, Help's browser and an LCD page. It reads help without
starting hardware apps or changing files/settings apart from normal history.

The pager navigation regression compiles the actual row traversal functions and
checks forward/backward symmetry over widths 1–100, wrapped words, tabs, long
tokens, blank lines and CRLF. This guards the end-of-page/backward-scroll fix
exposed by the full manual acceptance test.

### On-demand task RAM checks

Run `python3 tests/ports/test_teensy41_task_memory.py` from the repository root
for actual worker-code allocation failure, task creation rollback and safe
stack reclamation tests. `test_teensy41_compose_host.sh` also exercises OCRAM
allocation/alignment/reallocation and PSRAM reserve policy. The device Telnet
suite now verifies listener stack allocation, exact OCRAM recovery on stop,
self-stop, and stopping during remote Python. See the handoff memory audit.

### On-demand audio ring checks

`python3 tests/ports/test_teensy41_audio_memory.py` exercises the actual audio
ring implementation under sanitizers, including failure/overflow/cancellation,
interrupt handoff and cross-console ownership. Device check:
`python3 scripts/ports/test_teensy41_audio_memory.py --log /tmp/teensy-audio-rings-device.json`.
It uses unique SD fixtures and removes them on success. With no codec, it reports
missing-codec/idle-memory coverage only; live capture/playback requires SGTL5000.

### Live audio acceptance

With SGTL5000 connected at boot and the USB serial monitor closed:
- `test_teensy41_audio.py --fixtures DIR --log LOG` plays generated fixtures
  from `test_teensy41_audio_host.sh`, tests cancellation/restarts, and cleans SD.
- `test_teensy41_mic.py --ram --log LOG --wav WAV` captures the diagnostic tone
  in temporary RAMFS, downloads it, tests cancellation/header protection, then
  measures acoustic tone energy. Without `--ram`, `--storage sd|flash` selects
  persistent media; an acoustic failure is separate from successful transport.
- `test_teensy41_audio_storage.py --log LOG` verifies 4/30/180-second SD
  recordings, exact WAV headers/length, zero overruns, elapsed time, cancellation,
  overwrite protection, invalid destinations and exact heap recovery. It removes
  unique fixtures and does not retain ambient audio. Use `--seconds 30 600` for a
  longer run; `--storage flash --seconds 4` is an opt-in characterization only.
- `test_teensy41_radio_live.py --fixture MP3 --log LOG --wav WAV` serves a generated
  128 kb/s MP3 over local HTTP, plays it on the LCD console and captures four
  seconds of microphone input in RAMFS. `--url URL` instead tests an actual
  station without changing the saved catalog. Inspect counters and the recording;
  arbitrary music silence alone cannot establish a dropout. Controlled tone
  analysis uses 100 ms windows to separate the 420 Hz hum harmonic and requires
  tone energy above adjacent-band noise; an auto-off speaker is inconclusive.
  Host WAVs can contain
  ambient microphone audio. Successful temporary fixtures are removed.


### Folder player

The shared solarOS `player` is available on integrated Teensy display builds.
Use `player /sd/music` or `player --shuffle --repeat all /sd/music`.
Ctrl+Z leaves music playing; open `edit`, suspend it, then use `sessions` and
`fg ID` to return to either app. Details and controls are in
[teensy41-player.md](../../doc/ports/teensy41-player.md).

`bash scripts/ports/test_teensy41_player_host.sh` and
`python3 tests/ports/test_teensy41_player_controls.py` cover folder ordering,
shuffle, limits, allocation errors and repeat transitions under sanitizers.
`python3 scripts/ports/test_teensy41_player.py --log LOG` needs host `pyserial`,
`pyte` and `ffmpeg`, plus audio hardware, SD, Ethernet and exclusive USB.
It serves generated tones over local HTTP,
checks their hashes, tests controls, background playback while editing/saving,
and exact idle memory recovery, then removes its unique fixture directory.

## FTP client and server — 2026-10-05

Build `teensy41_telnet_legacy` for the existing wiring. Run
`bash scripts/ports/test_teensy41_ftp_host.sh` for sanitized adapter, loopback
client/server, stalled-transfer cleanup and job-command tests. These use temporary
host files and local TCP sockets; LeakSanitizer needs an environment that allows
thread inspection. See [FTP usage and pending live checks](../../doc/ports/teensy41-ftp.md).
Live acceptance (idle board, SD mounted, host on the Ethernet LAN):

```sh
/tmp/solaros-ssh-testenv/bin/python scripts/ports/test_teensy41_ftp.py --log /tmp/teensy-ftp-live.json
/tmp/solaros-ssh-testenv/bin/python scripts/ports/test_teensy41_ftp_client.py --log /tmp/teensy-ftp-client-live.json
/tmp/solaros-ssh-testenv/bin/python scripts/ports/test_teensy41_ftp_coexistence.py --log /tmp/teensy-ftp-coexistence-live.json
/tmp/solaros-ssh-testenv/bin/python scripts/ports/test_teensy41_ftp_player.py --music /sd/music --local /sd --host 192.168.1.100 --log /tmp/teensy-ftp-player-live.json
/tmp/solaros-ssh-testenv/bin/python scripts/ports/test_teensy41_ftp_client.py --music /sd/music --log /tmp/teensy-ftp-player-transfers.json
```

These require host USB/network access. The client test also requires `pyte` and
`pyftpdlib`; the server test uses Python's `ftplib`. The player startup regression
uses existing `--music` and `--local` directories read-only, and an anonymous
server supplied with `--host`. Client `--music` enables playback during transfers
and repeated server cleanup. Require idle consoles and run tests sequentially.
The transfer/server tests create uniquely named SD fixtures, remove them after success, and retain paths
in the JSON report on failure. The server test cycles Ethernet down/up; the
coexistence test starts a temporary Telnet service and runs `poweroff --check`
without cutting power. The FTP test console accepts asynchronous log messages
after the shell prompt. The TUI test waits for the exact `connected` footer,
which avoids confusing it with the initial `disconnected` title.

### Interactive recorder

`python scripts/ports/test_teensy41_recorder.py --log /tmp/recorder.json` tests
LCD capture controls, pause, retained recording and remote close, WAV validity,
existing-file protection, playback, monitoring and repeated memory recovery.
Requires an idle audio-equipped Teensy, mounted SD and exclusive USB access.
Creates a unique `_recorder_*` directory and removes it after success; the JSON
report preserves fixture paths on failure.

`test_teensy41_recorder_serial.py --log REPORT.json` additionally requires pyte
and checks USB UI rendering, gain setup, monitored recording and file errors.
Run recorder device tests sequentially. The deterministic host counterpart is
`python3 tests/ports/test_teensy41_recorder_transport.py` (ASan/UBSan).

### Concurrent multi-console investigation

`test_teensy41_multitask.py` exercises MP3 playback, detached Python CPU/SD
logging, FTP transfers, and Telnet-owned SSH against an isolated host fixture.
It supports reduced combinations for fault isolation and logs byte comparisons,
audio counters, latency, memory, and cleanup. Requires `pyserial`, `paramiko`,
idle consoles, LAN access, and `/sd/music` (or `--music`). Example:

```sh
python scripts/ports/test_teensy41_multitask.py --mode no-ssh --soak-only \
  --seconds 120 --payload-kib 64 --log /tmp/teensy-multitask-no-ssh.json
```

The original firmware hung during concurrent SSH/FTP. Moving the permanent
console stack from DTCM to OCRAM increases network allocation headroom; verify
the linked placement with `python3 scripts/ports/test_teensy41_console_stack_host.py`
(defaults to the `teensy41_telnet_legacy` ELF and its 40,960-byte stack).
Successful device runs remove their unique SD fixtures; failures preserve evidence. Read the
[investigation results](../../doc/ports/teensy41-multitask-testing.md) before
repeating the failing combinations. No physical USB host keyboard is required.
