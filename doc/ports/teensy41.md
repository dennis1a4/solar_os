# SuperKeyboard CPU v2: i.MX RT1062 / Teensy 4.1

For milestones, checklists, and future ideas, use the
[Teensy progress tracker](teensy41-roadmap.md).

This is the **first bring-up port**, based on SolarOS commit
`5e1ddf2200055a6bdfdc7ae0664fd26f00e98b51`. It is not a completed SolarOS
platform, nor a fully hardware-validated firmware release. Basic bare-board
testing was performed on 2026-09-24; see the results below.
The `teensy41_shell` target now runs the shared shell, calculator, editor and
a Teensy MicroPython adapter through one USB session with writable SD storage.
The complete command set, persistent settings, storage hot-removal recovery,
and full scheduler/session manager remain unported. The original `teensy41` target remains the bring-up/recovery console.

## Build

From the repository root:

```sh
pio run -e teensy41
```

Outputs: `.pio/build/teensy41/firmware.elf` and `firmware.hex`.
The separate `teensy41` environment overrides the inherited ESP-IDF build,
uses an explicit source allowlist, and leaves existing ESP32 targets intact.
The target is under `src/platform/imxrt1062/teensy41/`.

FreeRTOS and its compatible Teensy platform are pinned by commit. The custom
platform is necessary because the stock core and FreeRTOS compete for SysTick
and PendSV. See [the port's explanation](https://github.com/tsandmann/freertos-teensy).
The platform currently also downloads transitive core/compiler Git branches;
recorded versions for this build are:

- Teensy platform: `f2a89fab6fbe936dc383a6565872eb30545984a6`
- FreeRTOS Teensy: `b1943409de50e0514469ae0fc69a6f9ae17f6118` (11.2.0-4)
- Teensy core: `d885618c45cf4accdd5be1a9765c9932dc367e98`
- ARM compiler package: `e9efc43919ffc4f1eedea7f26c50503d4675e86b`

The optional `teensy41_peripheral_check` environment compiles candidate RA8875,
ST7735, SGTL5000 and USB host integrations. **It is a compile check, not a
recommended image to upload.** Display identification and audio wiring must
be resolved before enabling these options on the board.

Libraries retain their own licenses. In particular, the FreeRTOS kernel is
MIT, while some Teensy port files and the bundled RA8875 driver have GPL
notices; do not assume the complete linked firmware is Apache-only.

## Upstream USB shell

The tested bring-up baseline is saved in local commit `b46367f`. Its original
HEX/ELF are also backed up outside the repository in
`../solar_os-baselines/2026-09-26/`. The separate shell target builds with:

```sh
pio run -e teensy41_shell
pio run -e teensy41_shell -t upload
pio device monitor --port /dev/ttyACM0 --baud 115200 --raw --exit-char 28
```

Use a VT100/ANSI-capable serial terminal with at least 80 columns and 24 rows.
The initial geometry is 80×24. For another size, run `stty size` in a separate
Konsole tab (it prints rows then columns), then `setterm size COLS ROWS` in
SolarOS. This setting is manual, session-local, and must match the actual window
when using the full-screen editor. PlatformIO
needs `--raw` to pass escape codes through to Konsole. `--exit-char 28` makes
Ctrl+\ exit the monitor, leaving Ctrl-C available to the shell. Close automated
tests before opening a monitor. The USB serial device number may change.
Opening USB CDC starts a fresh shell session with this prompt:

```text
Welcome to SolarOS
user@teensy41:/
```

Try `help`, `apps`, `echo hello`, `mem`, `uptime`, `ls /`, `cat /test.txt`, and
`calc -e "2 + 3 * 4"`. `calc` opens the actual upstream interactive calculator;
enter expressions, then use `:quit` or Ctrl+] to return to the shell. Arrow-key
history/editing, backspace, Ctrl-C input cancellation and command-name Tab
completion are supported. The prompt has a trailing space, not a `>` marker.
`exit` refuses to close the only shell, keeping the console available.

The new target compiles `src/apps/solar_os_shell.c`, its shared I/O, parser,
line/completion/launch helpers and filesystem commands, the real application
registry, and `src/apps/solar_os_calc.c`. `SOLAR_OS_SHELL_CORE_ONLY` selects a
small command table and command-name completion; default ESP builds retain the
full tables. Headless builds exclude display rendering from shell I/O and the
calculator. The single USB runtime uses the upstream port/stream registry,
VT100 decoder, shell session API and app lifecycle; it does not yet implement
the full multi-session `solar_os_port_shell` scheduler or background workers.

SD is mounted at `/`. The adapter supplies `funopen` streams, directory
iteration, and a 16-slot descriptor table shared with MicroPython. Supported
operations include read/write/append/update modes, seek, flush, mkdir, copy,
rename and removal. `mkdir`, `cp`, `mv` and `rm` use the upstream shell handlers;
`cp` and `mv` refuse existing destinations in this first profile. This is a
single-task SdFat bridge, not a complete POSIX VFS: metadata/permissions are
synthetic, seek is limited to signed 32-bit offsets, and hot removal and shared
access from other tasks are unsupported. Insert the card before boot.

The upstream editor uses PSRAM for its 256 KiB document buffer when fitted.
Save writes and syncs a sibling `.edit-tmp`, then swaps the previous file via
`.edit-bak`. Existing recovery files block a save so they can be inspected with
`cat`, `mv` and `rm`. This preserves the original on a write failure but is not
power-loss-atomic on FAT. Do not remove the card during use.

To create and run a script directly on the board:

```text
mkdir /scripts
edit /scripts/hello.py
```

Type `print("Hello from Teensy!")`, press Enter, **Ctrl-S** to save, and
**Ctrl-Q** or **Ctrl+]** to return to the shell. Then run:

```text
python /scripts/hello.py
python
```

`python` starts a basic ASCII REPL; backspace works, compound statements use
`...` continuation prompts and a blank line to execute. Ctrl-C interrupts a
running Python loop; Ctrl-D exits the REPL. `python -c "print(6*7)"` also works.
Each launch starts a fresh interpreter. SD file I/O, imports, script arguments,
and bundled modules including `math`, `json`, `gc`, `struct`, `binascii`,
`hashlib` and `random` are enabled. The heap is 512 KiB and requires PSRAM;
it does not expose all 8 MiB to a script. Source imports search the script's
directory, shell working directory and root. This is MicroPython, not
CircuitPython: `machine`, `board`, device drivers, `input()` and
SolarOS's ESP-specific Python bindings are not integrated. The network profile
adds IPv4 TCP sockets as described below. Busy execution
consumes serial typeahead to detect interruption. Direct `.mpy` app launch is
not offered. The vendored interpreter sources and generated tables are reused
without modifying the generated engine; `scripts/platformio_teensy_micropython.py`
builds them with the Teensy runtime in `python.c`.

Storage and the synchronous interpreter are owned by the one shell task.
Shell history can now persist under `/.shell/history`; persistent configuration
still reports unsupported, identity remains `user@teensy41`, and automatic
startup scripts are disabled. A reconnect starts a fresh shell and closes the
foreground app, discarding unsaved editor changes. Manual SD mount/recovery
commands remain in the recovery console. Added QSPI flash and Serial1 shell
input remain outside this target.

See [the hardware tests](../../scripts/ports/README.md) for a repeatable upstream
shell check. The bootstrap test script expects a different prompt and must only
be used with the original `teensy41` target.

## Rev D audio shield and aplay — hardware verified

The user wired a PJRC Audio Adapter Rev D to the bare Teensy. Output was
verified through a battery-powered portable speaker connected to the headphone
jack, with no charging connection; the user heard clear, quiet tones.
This is separate from the custom SuperKeyboard codec circuit and its unresolved
VDDIO issue below. Use the Rev D shield's **3.3V** and **GND**, plus matching
Teensy/shield pins 18/19 (I²C), 7 (audio out), 20 (LRCLK), 21 (BCLK), and 23
(MCLK). Pin 8 supplies the microphone input path. Connect the external microphone
to the shield MIC/GND pads. Wire
with power disconnected and keep clock wiring short. Keep the SD card in the
Teensy's native SDIO socket. The shield's SPI SD socket is not used.

The initial output is the headphone jack; line-out is muted. The headphone
virtual ground must not be tied to ordinary ground or a grounded amplifier.
Use the shield's line-output pads when adding an amplifier.
[PJRC shield documentation](https://www.pjrc.com/store/teensy3_audio.html).

Build the separate profile:

```sh
pio run -e teensy41_audio
```

The audio profile was uploaded and hardware-tested on 2026-09-26. Commands:

```text
audio status
audio tone
aplay -v 10 /music/song.mp3
```

`audio tone` produces a quiet 440 Hz tone for approximately one second;
`audio off` stops it early. `aplay` accepts 16-bit PCM WAV and MP3, including
mono/stereo and sample-rate conversion to the fixed 44.1 kHz stereo output.
Ctrl-C, Escape or Ctrl+] cancels playback. Default headphone volume is 20%;
`-v` selects 0–100. Generated test files are deliberately attenuated and the
hardware suite uses 10%, so its tones are quiet. Normal recordings may be much
louder; increase the playback and speaker volume gradually. Line-out selection,
background playback and the full audio stream/device service are not enabled.

Microphone recording is available as mono 44.1 kHz, 16-bit PCM WAV, with 20 dB
mic gain:

```text
arecord -d 5 /voice.wav
aplay -v 30 /voice.wav
```

Use a new filename: recording refuses to overwrite existing files. Without
`-d`, recording continues until Ctrl-C or the one-hour limit. Cancellation
finalizes the partial WAV. Only the `mic` capture source is supported. There is
no live microphone monitoring. A 32 KiB capture ring absorbs short SD delays;
capture overruns stop recording with an error. `audio status` reports overruns.
`audio mictest /new-test.wav` records four seconds and plays a one-second
440 Hz tone during capture, for a nearby speaker/microphone check. The tested
setup captures the tone, but has strong 60 Hz hum that needs investigation.

The implementation links the actual upstream `solar_os_audio_apps.c` with a
synchronous execution option, the existing MP3 codec and PCM converter, and a
Teensy SD transport/output adapter. The synchronous option defaults off for
other platforms and keeps all file operations in the one console task. A
32-slot stereo PCM ring lives in OCRAM; the Audio library ISR copies into its
internal audio blocks and drives I²S/DMA. Decode buffers use PSRAM. Cancellation
and a bounded output wait keep a stalled sink from waiting forever. Diagnostics
report output blocks and source starvation events, not measured analog quality.
The console stack is 32 KiB for this target; compiler stack-usage output reports
16,576 bytes in `mp3dec_decode_frame` alone.

The working SD/editor/Python shell is preserved in commit `4839f71` and
`../solar_os-baselines/2026-09-26-writable/`. The default `teensy41_shell`
profile keeps audio disabled. Host audio tests generate original quiet tones
with FFmpeg, run actual MP3/WAV decode/resampling, and check cancellation,
allocation failure, malformed/truncated inputs, output errors and cleanup
under AddressSanitizer/UndefinedBehaviorSanitizer. Hardware codec detection, audible tones, initial zero-underrun playback and
20 repeated plays passed; see the detailed results below. Recording, line-out,
channel separation and longer music/USB soak testing remain unverified.

## Hardware evidence

Inspected the current KiCad hierarchical schematic using `kicad-cli sch export
netlist --format kicadxml`, rather than relying on the old saved `.net` file.
Read `Pins_v2.ods` (capital P on disk). The extracted cross-reference is
[superkeyboard-v2-pins.json](superkeyboard-v2-pins.json).
Hardware documents and existing sketches were not edited.

| Function | Teensy pins / bus |
| --- | --- |
| Console | Serial1 RX 0, TX 1; USB device CDC also available |
| NES / shift registers | CLK 2, NES latch 3, NES data 4; shift latch 5/data 6 are connected in schematic although blank in spreadsheet |
| Primary display / slot 2 | SPI0 MOSI 11, MISO 12, SCK 13; CS candidate 9; reset candidate 14; wait candidate 15; Wire2 24/25 |
| Audio | SGTL5000, I2S OUT 7, IN 8, LRCLK 20, BCLK 21, MCLK 23; Wire SDA 18/SCL 19 |
| Expansion slot 0 | SPI1 CS 37, Serial7 RX 28/TX 29, Wire2 SDA 25/SCL 24 |
| Expansion slot 1 | SPI1 CS 36, Serial8 RX 34/TX 35, Wire1 SDA 17/SCL 16 |
| Shared SPI1 | MOSI 26, SCK 27, MISO 39 |
| Secondary display | SPI1; reset 30, D/C 31, CS 32, backlight 33; J21 also exposes Wire1 |
| Other | Motor standby 10, relay 22, voltage sense 38, amp shutdown 40, volume 41 |
| Storage | Teensy onboard SD socket uses native SDIO; pads 42–47 reserved |
| External RAM | Teensy onboard QSPI PSRAM, pads 48–54 reserved; 8 MiB detected and small-buffer tested on 2026-09-26 |

Pin numbers here are Teensy GPIO numbers, **not KiCad symbol pad numbers**.
No LED heartbeat is used: GPIO13 is the primary SPI clock.
Slot 0 and slot 1 card/port connectors are parallel electrical connections,
not independently selectable slots. No hot-plug detection or power switch is
present in the exposed interface. Do not auto-probe arbitrary SPI cards.

### Unresolved wiring and population

- GPIO9 is described as "Display Light PWM" but its schematic net is `slot2CS`.
  It cannot serve both roles. The optional RA8875 driver assumes CS=9,
  reset=14, SPI0, 800×480 based on the historical `DisplayTest_v1` sketch;
  that older sketch used different CS/reset pins and is not proof of v2 wiring.
- The 1.8-inch secondary connector does not identify its controller. ST7735R
  black-tab is only a candidate configuration; resolution, tab and orientation
  require confirmation. Optional driver uses polling transfers, no framebuffer.
- **U10 SGTL5000 VDDIO (pad 20) is on +5V in the current schematic export.**
  NXP gives 3.6V maximum for VDDIO. Check the assembled board and correct this
  connection before powering/testing audio. Firmware disabling audio does not
  remove voltage from that rail. [NXP datasheet](https://cache.nxp.com/docs/en/data-sheet/SGTL5000.pdf)
- SGTL5000 address jumper JP6 and DIN/DOUT routing jumpers JP7/JP8 must match
  the standard Teensy audio pin mapping; the optional codec defaults to 0x0A.
- LM4871 shutdown is active **HIGH**, despite the `ampEn` label and inverted
  schematic pin label. GPIO40 stays HIGH; relay and motor-enable stay LOW.
  [TI datasheet](https://www.ti.com/document-viewer/lm4871/datasheet)
- `+3V3` and `+3.3V` appear as distinct nets in the schematic export; verify
  their intended supply connections on the actual board.
- Slot 0 UART pins can alternatively drive RFM95 reset/IRQ, nRF24 or CAN
  signals; slot 1 overlaps IR/encoder functions. Choose one installed role.
- USB host power/routing and any external hub need verification. USB-C power
  negotiation through STUSB4500 is not implemented; firmware does not change
  its stored power configuration.

## What this port currently does

| Requested milestone | Current implementation / remaining work |
| --- | --- |
| Platform and core | ARM firmware links actual upstream `solar_os.c` app/context lifecycle, queues, shell parser/line utility, expression engine. This is a core subset, not the upstream `core` flavor. |
| Boot FreeRTOS | Startup creates console + heartbeat tasks and a queue. USB console and advancing heartbeat observed on a bare Teensy 4.1. |
| USB or Serial1 output | CDC and Serial1 at 115200, with no wait for USB enumeration. Both feed one bootstrap session. |
| SolarOS shell prompt | Recovery target retains the bootstrap prompt. `teensy41_shell` runs the upstream shell with a reduced command table and one USB session; multi-session support remains. |
| SDIO / mount | Recovery target uses `SD.begin(BUILTIN_SDCARD)` and direct commands. Shell target adds writable streams/descriptors and directory adapters for file commands, editor and Python; full VFS and recovery remain pending. Card insertion/removal recovery after successful mount is not implemented. |
| Primary display | Optional RA8875 text bring-up; controller/wiring confirmation and SolarOS terminal/GFX rendering still needed. |
| I²C / SPI / UART | Board adapters with per-bus locks and explicit SPI settings, bounded sizes, repeated-start I²C, slot UARTs. Upstream service API integration still needed. |
| Expansion | Pin descriptors and exclusive slot claims; no upstream expansion manifest/driver registry integration or auto-discovery. |
| PSRAM | SolarOS allocation API mapped to external pool with explicit internal fallback policy. `psram` command checks 4 KiB with cache writeback/invalidation. Fitted 8 MiB detected and used by editor/Python; full-capacity validation remains. |
| Audio | Optional SGTL5000/I2S 440 Hz headphone tone, off initially; speaker amp stays shut down. Electrical fix/confirmation, input/stream service and resource integration pending. |
| Secondary display | Optional ST7735 startup text; actual controller not confirmed. Full second-terminal support pending. |
| USB functionality | CDC console; optional host hub + ASCII keyboard input. USB disk, device HID, MIDI, networking and full SolarOS input events not implemented. |
| Higher applications | Shell target runs the full calculator in text mode and one-shot evaluation through the upstream app registry/lifecycle. Editor and a Teensy MicroPython runtime are enabled; other apps and graphics remain disabled. |

The application task is the sole owner of display, SD and USB-host polling.
Bus wrappers use mutexes for future task callers; callers must not bypass the
wrappers/locks through Arduino globals. Core task stacks and RTOS objects stay
in internal RAM. PSRAM allocation is for ordinary CPU data only. DMA allocation
requests explicitly fail until a cache-coherent allocation/synchronization
contract is implemented. Memory status reports zero for unavailable largest
block/minimum-free statistics, not a measured value. Internal free estimates
include newlib free blocks and the uncommitted heap range.

## Hardware bring-up procedure

1. Resolve the supply discrepancy above before powering the assembled audio
   hardware. Confirm panel controllers, pin 9 role, PSRAM population and USB
   host routing. Keep optional peripheral flags disabled for initial testing.
2. Build `teensy41`. Connect the Teensy's programming USB port. When ready to
   replace existing firmware, use `pio run -e teensy41 -t upload`.
   A bootloader button press may be needed.
3. Open USB CDC or Serial1 at 115200 8N1. Press Enter if connected after boot.
   The Serial1 connector has jumpers/divider options; verify voltage/routing.
4. Look for `FreeRTOS console task running` and `solaros[teensy41]>`.
5. Run `info` twice a few seconds apart: uptime and heartbeat must increase.
   `stack-free` should remain positive under representative workloads.
6. Run `calc "2 + 3 * 4"` (14) and `calc "sqrt(81)"` (9).
   Check quoting errors, backspace, Ctrl-C, CRLF, and line-overflow rejection.
7. Insert a FAT-formatted card before boot; run `mount`, `ls /`, and
   `cat /test.txt`. A missing card must not prevent the console from starting.
   These commands do not create/delete/write files.
   `sdinfo` shows the last actual mount attempt sequence, including driver error
   codes and elapsed time; reading it does not try to mount. Failed mounts now
   retry at most three times with 100 ms between attempts. A missing card took
   about 6.2 seconds to exhaust these attempts in testing. An already mounted
   card is not reinitialized by `mount`.
8. Run `mem`, then `psram`. A board without PSRAM must fail the external-required
   allocation cleanly. Repeat with fitted PSRAM; absence is not an error at boot.
9. `slots` shows wiring/owners. `i2c 0`, `i2c 1`, `i2c 2` probe only when requested.
   Test SPI and UART loopback with a known slot fixture before enabling drivers.
10. Enable confirmed peripherals one at a time; test primary display, shared-bus
    arbitration, audio on/off, secondary display and USB keyboard independently.

## Next integration work, in order

The baseline is preserved and the first upstream USB shell works. Next add
writable storage and persistent configuration with explicit failure/recovery
semantics; then extend command coverage and session management. Test the shell
without a card and across cold boots independently of the older bootstrap
results. Broaden PSRAM coverage and identify the fitted flash before choosing
its filesystem layout. Bridge board buses and slot ownership to the SolarOS
resource model, then bring up confirmed displays, audio and USB-host input.
Enable further apps individually, checking stack use and launch/exit cleanup.

## Reproduce schematic extraction

```sh
kicad-cli sch export netlist --format kicadxml --output /tmp/sk-v2.xml \
  /home/dennis/Documents/SuperKeyboard/SuperKeyboardCPU/SuperKeyboardCPU_v2/SuperKeyboardCPU.kicad_sch
python3 scripts/ports/inspect_superkeyboard.py --netlist /tmp/sk-v2.xml \
  --pins /home/dennis/Documents/SuperKeyboard/Pins_v2.ods \
  --output doc/ports/superkeyboard-v2-pins.json
```

## Validation recorded on 2026-09-20

- `pio run -e teensy41 -e teensy41_peripheral_check`: both succeeded.
- Baseline image: 161,744 bytes flash; RAM1 163,616 bytes, RAM2 14,296 bytes.
- Optional peripheral image: 222,888 bytes flash; RAM1 207,200 bytes,
  RAM2 18,992 bytes. These linker figures exclude subsequent dynamic allocations.
- `bash scripts/ports/test_teensy41.sh`: passed headless core lifecycle,
  allocation-failure/start-failure cleanup and expression tests; upstream shell
  parser, line editor and non-headless app-context regression tests also passed.
- `git diff --check`: passed. Third-party libraries emit compiler warnings;
  neither build is claimed warning-free.
- `pio device list`: no serial device listed. Scheduler boot, console output,
  SDIO transfers, PSRAM and every peripheral still require on-board testing.

## Bare-board validation on 2026-09-24

Tested a Teensy 4.1 connected directly to the computer, with an SD card and no
external peripherals. The baseline `teensy41` build replaced MicroPython.
The first upload required a button press; the subsequent upload rebooted the
board automatically. USB serial was available at `/dev/ttyACM0`.

- Baseline build/upload and host test script passed.
- USB bootstrap console responded; `info` reported FreeRTOS V11.2.0.
- Calculator results: `2 + 3 * 4` = 14, `sqrt(81)` = 9, `6 * 7` = 42.
- Backspace, Ctrl-C, CRLF, unknown commands, unterminated quotes, invalid
  expressions, and oversized-line rejection passed.
- SD mount, root listing and reading the existing 17-byte `/test.txt` passed.
  Missing-file and missing-directory errors returned to the prompt. No SD files
  were written or deleted.
- No PSRAM was detected; the external-required allocation failed cleanly.
- Hardware testing exposed incorrect heap accounting: Arduino's `__brkval`
  remains stale because the FreeRTOS runtime overrides `sbrk`. The adapter now
  uses the pinned runtime's actual heap start, maximum and current end, also
  correcting the total for the runtime's configured heap region.
- After rebuilding/reflashing, 200 repeated calculations passed. Reported free
  heap was unchanged at 334,328 / 356,544 bytes after warm-up. Heartbeat advanced
  from 20 to 44, and console stack high-water headroom remained 3,268 words.
  This is a short smoke test, not exhaustive leak or long-duration validation.

Observed limitation in the initial smoke test: after the second flash, SD was not mounted at startup.
An explicit `mount` succeeded and subsequent reads passed. Automatic SD startup
mount reliability across warm resets needed investigation. At that point,
hot-removal recovery, boot without a card, cold power cycling, fitted PSRAM and
external peripherals were untested. The test client needed a one-second delay after opening USB
serial before sending its first command.

### SD startup follow-up

The baseline passed a cold boot with the card inserted, one warm restart, and
a cold boot without the card. The earlier intermittent SD failure did not
reproduce in that warm restart; its root cause is not yet established.

The storage adapter now tries initialization at most three times, separated by
100 ms RTOS delays. `sdinfo` preserves each driver's error code/data and elapsed
time from the last actual mount sequence. This provides bounded recovery for
transient initialization failures and diagnostics if the issue recurs; it is
not proof that the original failure's cause has been fixed. A successful mount
is still cached, so removing/replacing an already mounted card remains unsupported.

With the updated firmware and no card, startup and an explicit `mount` both
exhausted three attempts in 6,203 ms, reporting card error `0x17` (ACMD41) and
data `0x00018000`. The console and calculator remained usable afterward.
Inserting the card into the powered, idle board then running `mount` succeeded
on the first attempt in 390 ms. Repeated directory/file reads and calculator
checks passed, with reported free heap unchanged at 334,328 bytes.

After removing a USB extension (same main cable), the updated firmware also
passed cold startup with the card: automatic mount succeeded on the first
attempt in 390 ms. Two requested 1,000-cycle stability runs were interrupted
by host-visible USB disconnects after 535 and 287 complete calculator/list/read
cycles, respectively. All completed file reads matched the initial content.
The board was reported untouched. Uptime progressed from 28,515 ms before the
first run to 133,805 ms afterward, consistent with elapsed wall time, so that
disconnect did not appear to reboot the firmware. A follow-up short check passed
with free heap still 334,328 bytes. Linux reported USB power control `on` and
runtime status `active`, so autosuspend was not enabled for this device.

These runs are **not stability passes**. A different USB data cable is the next
isolation check; connection/host/firmware causes remain open. Firmware HEX SHA-256
for these runs: `35025fd5868e0ca0f5e568802b70dc0cae159ca67d84eeaaf772ecbb61680269`.

Repeatable tests are in `scripts/ports/test_teensy41_serial.py`; see the
[hardware test instructions](../../scripts/ports/README.md). They discover the
USB serial port, save JSON transcripts, and require no particular SD file;
an optional existing small text file can be selected for repeated read checks.

### New USB cable/port and fitted PSRAM — 2026-09-26

User fitted W25Q128JVSIQ flash and Teensy-store PSRAM, believed to be 8 MB,
and connected a different USB data cable to a different host port. No firmware
was uploaded during this session. The local baseline HEX still hashes to
`35025fd5868e0ca0f5e568802b70dc0cae159ca67d84eeaaf772ecbb61680269`.

- The unchanged 1,000-cycle calculator/root-listing/`/test.txt` read workload
  passed in 154.003 seconds without a USB disconnect. Automatic SD mounting
  had succeeded on attempt one in 392 ms. Uptime advanced from 112,661 to
  265,555 ms; heartbeat advanced from 110 to 263. Internal free heap stayed
  at 334,328 bytes and final console stack headroom was 3,274 words.
- `mem` detected 8,388,608 bytes (8 MiB) of PSRAM, with 8,388,600 bytes free.
  A separate run passed 100 calculator/SD-read cycles with the existing
  cache-flushed 4 KiB `psram` test before the loop and after every cycle
  (101 successful PSRAM checks). Internal and reported external free memory
  were unchanged afterward. This tests repeated small allocations, not the
  full capacity, all address lines, or long-duration RAM reliability.
- Host test script now accepts `--psram`; syntax compilation and CLI help
  checks passed, followed by the hardware run above.
- Logs: `/tmp/teensy-new-cable-1000.json` and
  `/tmp/teensy-fitted-psram-100.json` on the test host. These include selected
  SD file contents and are not committed.

This meets the previously interrupted 1,000-cycle workload, but is only about
2.6 minutes of USB testing. Both cable and port changed, and memory was added;
the earlier USB failures' cause remains unproven. Longer soak testing is still
useful. Added flash has not been probed, formatted, or tested; the bootstrap
firmware has no added-flash interface. Flash identification/integration and a
broader PSRAM test remain follow-up work. Existing port changes remain
uncommitted and were preserved.

### Upstream shell hardware validation — 2026-09-26

`teensy41_shell` built, uploaded, and passed the dedicated hardware test with
1,000 calculator/SD-read cycles in 103.774 seconds. The board is
left running this image. Tested behavior includes `help`, `apps`, quoting and
unknown-command diagnostics, backspace, Ctrl-C, arrow-key history, command-name
Tab completion, CRLF, full interactive calculator entry/exit via Ctrl+] and
`:quit`, one-shot calculations and invalid expressions/options, `cd /`, missing
paths, root listing, and repeated reads of the existing `/test.txt`.

- Final test log: `/tmp/teensy-upstream-shell-final-1000.json` (host-local).
- Uptime: 29,299 → 131,965 ms; final stack high-water
  headroom: 5,460 words. No USB disconnect during the test.
- Internal free heap: 269,924 / 301,216 bytes; PSRAM free:
  8,385,240 / 8,388,608 bytes. Both unchanged across the loop.
- Image: 201,116 bytes flash; RAM1 218,944 bytes; RAM2 14,532 bytes.
- HEX SHA-256: `ad79deab8794ead5c400e3df65af49e5239d408dd1012dcd0be790fe06e5576d`.
- `bash scripts/ports/test_teensy41.sh` passed lifecycle/failure cleanup,
  parser, line editor, non-headless context/I/O and new path-normalization tests.
- Recovery `teensy41` target rebuilt successfully. Default graphical calculator
  and full-shell paths passed host syntax checks (with declarations supplementing
  the older host test headers); this is not an ESP32 firmware build/test.
- `git diff --check` passed. Third-party and existing upstream warnings remain.

This validates the first upstream shell, not all commands/services or all
hardware conditions. Shell-specific cold-start/missing-card checks, long soaks,
full PSRAM coverage, writable storage and persistent settings remain pending.

### Calculator cursor correction — 2026-09-26

User testing in Konsole exposed a visual defect missed by the original serial
test: the target tracked 24 rows but the host window could be taller. After
sufficient output, calculator redraws used absolute row 24 while the actual
prompt was farther down, overwriting earlier output. Stripping ANSI codes from
test transcripts verified result text but could not detect misplaced text.

Calculator input now uses the shared atomic line redraw helper. Redrawing the
current port line uses carriage return and horizontal motion, preserving the
actual terminal row regardless of a height mismatch. Explicit redraws of other
rows retain absolute positioning. Backspace and left/right editing remain
supported. The configured width is still 80 columns; keep at least 80 columns
for this initial profile. This does not add terminal-size negotiation for future
full-screen applications.

A host regression renders output from the actual calculator and shell I/O code
into simulated screens with 16, 24, 26, 40 and 60 rows, before and after scrolling.
It reproduced the old failure and passes with the fix, checking input, backspace,
arrow positioning, results and preservation of the surrounding output. It also
checks column-zero redraw and explicit redraw of another row. The existing
host suite and shell build pass.

The cursor fix was uploaded and passed the USB shell suite plus 100
calculator/SD-read cycles in 13.096 seconds. The board's captured
interactive-calculator output now uses horizontal-only redraws. Reported free
memory stayed unchanged; final stack headroom was 5,460 words.
Log: `/tmp/teensy-cursor-fix-100.json`.
Flashed HEX SHA-256:
`130bc9660107186cdbf9adb10f9588e9de904b9ef4d22bf85549ef2b1e0ecf5a`.

### Writable SD, editor and MicroPython validation — 2026-09-26

Working cursor-fix shell HEX/ELF were preserved before this work under
`../solar_os-baselines/2026-09-26-cursor/`. The new shell build was uploaded;
no added-QSPI-flash operations or card formatting were performed.

The writable integration suite passed on the fitted board/card:

- Create a Python source file in the actual editor, save, exit, read it back
  and execute it; replace an existing file and reopen it.
- Dirty-exit confirmation, preserved original when a recovery file blocks save,
  and editor launch at 80×24 and 100×40 geometry. These are hardware transcript
  checks, not full editor screen-layout or power-failure tests.
- Copy/move/remove, refusal to overwrite destinations (including a FAT case
  alias), file write/append/update/truncate/seek/flush, missing files and
  exclusive-create errors.
- REPL arithmetic, selected standard modules, SD imports and scripts, 300,000
  byte allocation followed by GC, Ctrl-C interruption of an infinite loop,
  division errors, out-of-memory errors and recursion-limit recovery.
- Exhaust all 16 file slots, then leave files open on interpreter exit and
  successfully reopen afterward, exercising finalizer cleanup.
- 50 editor/Python lifecycle cycles with unchanged reported free memory:
  internal 61,164 / 92,224 bytes; external 8,385,240 / 8,388,608 bytes.
  Deep-recursion stress left a minimum of 452 words of console stack headroom;
  that is the historical low-water mark, not current stack availability.
- Reboot, read persisted files and rerun the saved script. Fresh uptime was
  1,922 ms and minimum stack headroom 5,541 words. The first test-client attempt
  hit an I/O error during USB re-enumeration; reconnect retry resolved it.

Host lifecycle/parser/path/calculator-screen tests, editor key-policy tests,
non-headless TUI widget tests and shell launch tests pass. Recovery firmware
still builds. New shell size: RAM1 427,936 bytes, RAM2 15,236 bytes, flash
414,368 bytes. Python's heap and editor/diff buffers allocate from PSRAM at
runtime; internal RAM remains limited for future additions.

Logs: `/tmp/teensy-writable-extended.json`,
`/tmp/teensy-writable-reboot-retry.json`. Test files are retained under
`/_solaros_test_111186ef8a`; earlier development runs retained their own uniquely
named test directories. Cold power-loss recovery, surprise SD removal/full-card
writes, the entire PSRAM capacity, and missing-PSRAM behavior of the new apps
have not been hardware-tested.

Flashed HEX SHA-256:
`563157187083967df008f7354b76a24c570589b38bd5bc9ec82fbb8e3f77a2c4`.

Final shell/calculator/SD-read regression passed 100 cycles in
13.194 seconds with stable free memory (internal 60,924
bytes; external 8,385,240 bytes). Log:
`/tmp/teensy-writable-shell-100.json`. A final rebuild produced the identical
HEX hash above. The recovery image was compiled but not uploaded.

Audio preparation results (not flashed): `teensy41_audio` builds at 436,544 bytes
RAM1, 35,284 bytes RAM2, and 444,168 bytes flash. Prepared HEX SHA-256:
`f9e9e63607ed3f43a417dc1224d83f155d495b6543abc24d822511222cf14476`.
Extended audio host tests passed with sanitizers; quiet fixtures are retained
in `/tmp/solaros-audio.Yi7JBp`. The regular shell builds and its host regression
suite passes. Existing unused-function/truncation warnings remain in shared
shell/calculator code. On-board playback and listening checks remain pending.

### Rev D shield hardware validation — 2026-09-26

Uploaded the prepared `teensy41_audio` HEX (hash above). Rev D was wired by the
user; its SGTL5000 was detected. A portable speaker was connected to the 1/8-inch
headphone jack on battery power only. User confirmed clear tones and noted the
intentionally low level. This verifies audible output, not independent analog
left/right channel separation or measured signal quality.

- Generated fixtures transferred to SD through the board's Python interpreter;
  SHA-256 comparisons matched for each file.
- Stereo 44.1 kHz MP3: 1.131 s wall time, 369 output blocks, zero source underruns.
- Mono 48 kHz MP3 converted to 44.1 kHz stereo: 1.110 s, 364 blocks, zero underruns.
- Mono 22.05 kHz 16-bit WAV converted to stereo: 1.050 s, 345 blocks, zero underruns.
- Ctrl-C interrupted active playback; the next playback succeeded. Missing-file
  and invalid-volume errors returned to the shell. The first suite stopped on
  an expected-text mismatch for the missing-file error; correcting the test
  allowed the full suite to pass, without a firmware change.
- 20 further alternating MP3/WAV plays passed with stable reported memory:
  internal 44,364 / 83,616 bytes free; PSRAM 8,385,240 / 8,388,608 bytes free.
  Console stack low-water mark after the run: 3,791 words (15,164 bytes).

Log: `/tmp/teensy-audio-20.json`. Retained fixture directory on SD:
`/_solaros_audio_a6e6cf287a` (an earlier run retained its own test directory).
The tested audio image is backed up under
`../solar_os-baselines/2026-09-26-audio/`. In that image, line-out and recording are disabled;
custom SuperKeyboard audio wiring is still a separate bring-up task.

After audio playback, the shell/calculator/SD-read regression passed 50 cycles
in 8.659 seconds with stable reported free memory and the same 3,791-word stack
low-water mark. Log: `/tmp/teensy-audio-shell-50.json`. No USB disconnect occurred
during these playback or regression runs. This is a short validation, not a
long-duration music/USB soak.

### Microphone capture validation — 2026-09-26

Enabled the upstream `arecord` app with synchronous SD recording and the Rev D
I²S microphone input. Suppressed queued progress events in synchronous mode
to prevent long recordings from filling the event queue. The main MP3 decoder
runs from cached program flash to preserve internal RAM with capture enabled.

The nearby battery-powered speaker supplied the test tone. A four-second WAV
contained 176,400 mono samples; 440 Hz band energy rose 38.2 dB during the tone.
No clipped samples or capture overruns were detected. Strong 60 Hz hum was
present before, during and after the tone: capture works, but clean microphone
quality is not yet verified. Cancellation, subsequent recording and existing-file
protection passed. Host sanitizer tests also cover WAV headers and recording
cleanup. Initial capture log/WAV: `/tmp/teensy-mic-first.json` and
`/tmp/teensy-mic-first.wav`; these may contain ambient audio and are not committed.

Playback regression passed all three formats, cancellation and ten further
plays with stable memory: internal 43,212 / 82,464 bytes free and PSRAM
8,385,240 / 8,388,608 bytes free. Console stack low-water mark: 3,793 words.
Log: `/tmp/teensy-mic-playback-10.json`. Build size: RAM1 437,696 bytes,
RAM2 69,588 bytes, flash 448,080 bytes.

Uploaded HEX SHA-256:
`aa705da1b56dd80b558ed45b2cb2d054a90ceb9fa1f05f065e065cb022e22a76`.
Tested firmware backup: `../solar_os-baselines/2026-09-26-microphone/`.
Long recording, full-card, interrupted-power and surprise-removal tests remain.

A follow-up run verified that Ctrl-C leaves a WAV whose data length matches
the file, with unchanged free memory and zero overruns. Its acoustic check
failed: 440 Hz energy rose only 1.6 dB, and 60 Hz hum dominated. Speaker state
and position need confirmation before treating acoustic capture as repeatable.
Log/WAV: `/tmp/teensy-mic-final.json`, `/tmp/teensy-mic-final.wav`.

Microphone follow-up: standalone playback was audible when the combined test
was silent. Added an emitted-tone block counter and made the ISR-shared tone
deadline volatile. After uploading that diagnostic build, two consecutive
capture runs passed without rebooting between them: 345 tone blocks per run,
440 Hz energy increases of 42.4 and 44.1 dB, zero clipped samples and overruns.
Background RMS fell to roughly 41–49 sample units from thousands in the earlier
recordings. The cause of the earlier silence/hum is not established; these
results do not isolate a firmware fix from restart or physical setup changes.
Logs: `/tmp/teensy-mic-counter.json`, `/tmp/teensy-mic-counter-repeat.json`.
Diagnostic HEX SHA-256:
`8137813c248321a22464e7e58c35c76818da6dbd8d57d12ef2322506d6d07ce4`.
Backup: `../solar_os-baselines/2026-09-26-microphone-diagnostic/`.

The user confirmed hearing these tones and reported moving the Teensy slightly.
A physical connection issue is therefore plausible, but not confirmed. Inspect
and secure the wiring with power disconnected if silence or hum returns.

## Native Ethernet adapter — initial hardware checks passed

The user selected the PJRC Ethernet kit for the Teensy 4.1's native Ethernet
header. Wire with power disconnected according to the
[PJRC kit instructions](https://www.pjrc.com/store/ethernet_kit.html). The jack
requires its magnetics; this is not a direct GPIO-to-RJ45 connection.

`teensy41_network` extends the tested audio profile and adds QNEthernet 0.36.0
at commit `ce1977ecb7af916083d5d3435270bc5585e296a8`. QNEthernet is
AGPL-3.0-or-later; its own license is retained in the downloaded dependency.
The adapter initially exposes a reduced text command:

```text
network up
network status
network resolve example.com
network connect example.com 80
network down
```

Startup is manual. `up` starts DHCP without waiting for a cable or lease. Status
reports `eth0`, link, IPv4 address/mask/gateway/DNS, MAC and task stack headroom.
Connect checks only the TCP handshake to the specified endpoint and closes it;
it does not fetch a page. DNS and connect each have a three-second timeout.
There is no listening server or automatic outbound application connection.

A dedicated FreeRTOS task owns every QNEthernet operation and receives copied
shell requests through queues. Automatic polling through Arduino yield is
disabled because QNEthernet does not support concurrent callers. This lets
DHCP/packet processing continue while foreground apps run. Its static stack,
lwIP heap/pools and DMA buffers use RAM2. Ethernet/lwIP library code and constant
data are placed in cached flash by a generated linker script, preserving RAM1
for the console heap.

This is the first transport adapter, not full compatibility with ESP-IDF. The
shared network registry currently exposes `esp_netif` and ESP events; mapping
it and the socket APIs is a subsequent step. Wi-Fi scanning, SSIDs, AP/router
mode, TLS, MQTT and WireGuard are not enabled. The MicroPython TCP adapter
described below is now available.
Current Python and audio functionality remain compiled in.

Build: `pio run -e teensy41_network`. Hardware link/DHCP and DNS/TCP validation
remain pending until the kit is connected. The saved audio-only firmware remains
available under `../solar_os-baselines/2026-09-26-microphone-diagnostic/`.

Ethernet preparation build passed: RAM1 444,352 bytes (84.8%), RAM2 145,696
bytes (27.8%), flash 527,080 bytes. ELF inspection confirms Ethernet.loop and
lwIP tcp_input reside in flash at 0x6000xxxx/0x6001xxxx. Build log:
`/tmp/teensy-network-build.log`. Prepared HEX SHA-256: `e88451b74e4fa0c26f8b4d45e3248fb9d0db13b2f96c545a4d869813de0e6ba5`.
The hardware test script passes Python syntax compilation; it has not run
against the board yet. No network firmware uploaded during preparation.

The independent `teensy41_audio` profile also rebuilt successfully after these
changes; network code remains excluded there.

### Native Ethernet hardware validation — 2026-09-26

User connected the PJRC kit; uploaded `teensy41_network`. Link detection, DHCP,
DNS resolution of example.com, TCP connection to example.com:80, invalid-port
handling and software down/up with DHCP reacquisition passed. They passed again
after audio and shell tests. Network task stack low-water mark was 1,700 words
in the first suite. Ethernet remains enabled in this running session; restart
requires `network up` again. The test only connected TCP; it did not request
an HTTP page.

With Ethernet running, stereo MP3, resampled mono MP3/WAV, cancellation/error
recovery and three further plays passed with stable free memory (36,540
internal bytes; PSRAM 8,385,240 bytes) and console stack low-water mark of
3,793 words. Shell/calculator lifecycle, line editing/history and SD reads
passed 20 cycles, stable within that suite at 36,300 internal free bytes.
The differing suite measurements are not a long-duration leak test.

Logs: `/tmp/teensy-network-first.json`, `/tmp/teensy-network-audio.json`,
`/tmp/teensy-network-shell.json`, `/tmp/teensy-network-after-apps.json`.
Tested firmware backup: `../solar_os-baselines/2026-09-26-network/`.
Uploaded SHA-256: `e88451b74e4fa0c26f8b4d45e3248fb9d0db13b2f96c545a4d869813de0e6ba5`.
At that stage physical cable removal/reinsertion, prolonged DHCP renewal/traffic
and Python network APIs remained untested/unimplemented; subsequent results follow. No services listen
for remote shell or file access.

## MicroPython TCP client sockets

The network profile now adds `import socket` to each interpreter session.
Supported: IPv4 TCP `socket()`, `getaddrinfo(host, port)`, `connect((host, port))`,
`send`, `sendall`, `recv`, `settimeout`, `close` and context managers.
`AF_INET`, `SOCK_STREAM` and `IPPROTO_TCP` are available. Other families/types,
getaddrinfo flags and named service ports are rejected. UDP, listening servers,
TLS/HTTPS, socket streams/makefile and select/poll are not implemented.

Four socket objects can be live at once. Default I/O timeout is five seconds;
`settimeout(None)` waits indefinitely but remains interruptible. Zero timeout
makes receive/send return EAGAIN if no progress is possible; asynchronous
connect is not supported. DNS has a five-second maximum. `recv(n)` may return
short reads (up to 512 bytes); an empty byte string means EOF. `sendall` handles
partial writes. Use errno names rather than Linux error numbers on this port.

The Ethernet task owns every client and DNS lookup; copied requests and replies
keep interpreter buffers out of the worker. Nonblocking worker operations let
the interpreter check Ctrl-C between attempts. Ctrl-C aborts all Python sockets
in the current session. Explicit close, context-manager exit, GC finalizers and
interpreter exit release resources. Handles include generations so a stale
Python object cannot close a later socket. Link loss aborts existing connections;
create a new socket after link/address recovery.

See `examples/teensy41/http_fetch.py`. It resolves a host, reads a small plain
HTTP response (including headers, maximum 32 KiB), then saves it using exclusive
creation to avoid overwriting a file. It is a small HTTP/1.0 example, not a full
HTTP client: redirects, chunk decoding and TLS are outside its scope.

```text
network up
network status
python /http_fetch.py example.com 80 / /http-response.txt
```

Copy the example to SD before using the path above. A failed network operation
does not create the destination because the bounded response is collected in
PSRAM first. SD write failures can still leave a partial destination file.

### Python networking validation — 2026-09-26

The hardware suite passed hostname lookup, a 4 KiB binary sendall/recv echo,
byte-for-byte HTTP response-to-SD verification, refusal to overwrite an existing
file, connection failure cleanup, receive timeout, nonblocking EAGAIN, Ctrl-C
while blocked indefinitely, four-socket exhaustion, twenty GC cleanup cycles,
and interpreter exit with live sockets. Five repeated script fetches had equal
reported free memory: internal 36,284 / 75,552 bytes; PSRAM
8,385,240 / 8,388,608 bytes. Software network down/up recovered successfully.

The user physically unplugged Ethernet while Python was blocked in recv. It
returned OSError and the shell remained responsive. After reconnection a new
socket fetched the exact fixture response. This cable test ran before the final
small pending-connect cleanup/partial-send changes; the complete software suite
passed on the final firmware afterward.

A public HTTP fetch from example.com returned HTTP/1.1 200 OK and saved 829 bytes
including headers. The example was installed at `/http_fetch.py`; its host source
is `examples/teensy41/http_fetch.py`. Test files are under
`/_solaros_pynet_7e1a1739a5/`. Early development runs left other unique folders.
The initial suite failed due to expecting Linux's timeout number (110 rather
than the target errno value) and then assuming EMFILE was exported by the small
errno module. Correcting these test expectations allowed the suite to pass.

Logs: `/tmp/teensy-python-network-final.json`,
`/tmp/teensy-python-network-cable.json`. Network stack low-water mark after the
final suite: 1,370 words; console: 6,941 words before MP3 playback.
Build size: RAM1 444,608 bytes, RAM2 147,008 bytes, flash 531,512 bytes.
Final firmware SHA-256: `085e6b3e3acf213153b413cae326a35c5959c524d71b23f2c7696cb79c6b4e01`.
Backup: `../solar_os-baselines/2026-09-26-python-network/`.
Long-duration traffic/DHCP soak, send-buffer saturation and TLS remain outside
this validation. Shared ESP-oriented network registry APIs remain unported.

Final audio regression on the socket firmware passed stereo MP3, resampled
mono MP3/WAV, cancellation/error recovery and three additional playback cycles
with stable memory. Log: `/tmp/teensy-python-network-audio.json`.
