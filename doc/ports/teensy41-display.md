# RA8875 SolarOS terminals

The `teensy41_display` profile extends the synth/apps profile with two independent
shell sessions: USB CDC and the Adafruit RA8875 LCD with a USB host keyboard.
Updated wiring profile (2026-10-07): `teensy41_display_wiring` uses main-display
SPI0 MOSI 11, MISO 12, SCK 13, CS 10, reset 14 and active-high backlight 15.
WAIT is disconnected. Motor-enable GPIO10 is disabled in this profile because
it is now display CS. Audio stays on AmpEn40 and Serial1 stays enabled.

The small ST7735 shares SPI0: SCL/SCK 13, SDA/MOSI 11, reset 30, CMD/DC 31,
CS 32 and backlight 33. These SCL/SDA labels mean SPI signals on this module.
Pins 16/17 remain Wire1 SCL/SDA for the motor controller and other I2C devices.
Both displays use the SPI0 mutex and separate chip selects. The small panel
shows a live CPU, RAM and storage dashboard using the 128x160
ST7735 preset. The initial black-tab
image produced a two-pixel strip at the right, a one-pixel strip at the bottom,
and clipped top-left text. A green-tab test corrected the edges but swapped red
and blue (user-confirmed 2026-10-08). The profile now keeps `INITR_BLACKTAB` RGB
ordering and explicitly sets column offset 2 / row offset 1 with
`setRowColStart(1, 2)`.
The user confirmed clean edges and correct RGB colors on 2026-10-08. Rotation
is now configured as `SK_SECONDARY_ROTATION=1` (90 degrees clockwise, 160x128);
the driver swaps the address offsets for the rotated orientation.

The dashboard samples CPU and DTCM/OCRAM/PSRAM used/total KiB once per second.
CPU is interval non-idle FreeRTOS time, rather than a task-list/stack scan.
OCRAM/PSRAM usage is tracked on allocation/free, including allocator overhead,
so reading RAM totals no longer scans those pools. Totals describe allocator
pools, excluding static/reserved RAM; they are not the chip capacities.
DTCM text turns yellow below 16 KiB free and red below 8 KiB. SD, USB and flash
used/total MiB are sampled every 30 updates; unavailable media are labeled.
Values are rounded down to whole KiB/MiB. USB uses the filesystem's cached
free-cluster count, without forcing a `df --refresh` scan. Storage queries run
outside the SPI mutex, but use the normal filesystem lock.

A priority-1 task uses a static 4 KiB OCRAM stack and no framebuffer. Changed
text rows are redrawn under separate SPI locks; the DTCM row also refreshes its
warning color. `lcd small` reports the displayed values, completed updates,
last update elapsed microseconds (including waits), cumulative task runtime
microseconds, SPI lock misses, and remaining stack high-water mark in bytes.
`lcd small off` pauses sampling/rendering and leaves the last image visible;
`lcd small on` resumes it. These settings last until reboot. Counters use
32-bit wraparound. Runtime deltas divided by wall time measure the monitor's
CPU cost; update elapsed time is not CPU time.

Backlight levels are controlled independently with `lcd brightness [0-100]`
and `lcd small brightness [0-100]`. Omitting the value reads the current level.
Zero turns the backlight off; 100 is full brightness. Both pins use hardware PWM.
Defaults are main 100%, small 50%; changes persist in flash under
`lcd_backlight` settings and are restored at boot. Invalid values are rejected.
A save failure is reported separately from the already-applied brightness.
These commands work while an app or graphics is active. `lcd small off` pauses
the dashboard; it does not change its backlight. A dark screen can be restored
from USB with the same brightness command.

The small display also runs the Game Boy app in `teensy41_display_wiring`:
enter `gameboy /sd/roms/2048.gb` at the main-screen keyboard. From USB, use
`lcd send "gameboy /sd/roms/2048.gb"`. The complete 160x144 Game Boy image is
scaled to 142x128 with black side borders. This initial Teensy port is silent;
the shared audio backend remains disabled. Arrows are the D-pad, Z/X are A/B,
Enter is Start, Backspace is Select, P pauses, R resets, and Q/Escape exits.
Held USB keyboard keys are sampled directly; injected/terminal keys use short
button pulses. The main LCD remains the owning console; USB stays independent.
The dashboard pauses while graphics owns the panel and returns on exit, retaining
its prior on/off setting. Backlight settings remain independent.

`lcd small benchmark` draws 120 moving RGB frames and reports elapsed/transfer
time and FPS. It refuses to run while the game owns the display. The measured
full-screen benchmark was 62.5 FPS at 24 MHz SPI (13.8 ms transfer per frame).
`lcd small gameboy` reports the latest emulator statistics. With the public-domain
[2048 homebrew](https://github.com/mmuszkow/2048-gb), the Teensy measured
59.73–59.76 emulated FPS and 29.86–29.88 displayed FPS, with no dropped frames.
Presentation includes scaling and takes approximately 15.4 ms per displayed
frame. The presenter runs synchronously in the emulator worker; alternate
emulated frames do not transfer a display frame. Other ROMs may perform differently.
ROM, emulator, display buffers and the worker stack are allocated on demand;
code is placed in cached flash to preserve DTCM headroom.

The test ROM and its Unlicense text are installed at `/sd/roms/2048.gb` and
`/sd/roms/2048-LICENSE.txt`. ROM SHA-256:
`f7e9a462df94d03a1248cc8ce1166df1aaa384b07e2f3ddc70bf81a88d78017b`.
This ROM has no cartridge save RAM. Battery-backed save handling is inherited
from the shared app but has not been exercised by this ROM.

The preserved `teensy41_telnet_legacy` profile still uses the former wiring:
SPI0 MOSI 11, MISO 12, SCK 13, CS 37, reset 9. Panel preset:
`Adafruit_800x480`. Touch is intentionally out of scope.

The LCD uses a 100x30 ANSI text terminal with the controller's 8x16 font, cursor,
color, inverse/underline attributes, erase and scrolling regions. The cell buffer
is allocated in PSRAM; dirty cells are rendered in bounded batches without a
full pixel framebuffer. A mutex-protected pending flag skips both the dirty-cell
scan and SPI acquisition when the terminal is unchanged. Writes mark pending
work; font changes and return from graphics force a repaint. Batches retain the
pending flag until a complete scan has drained dirty cells. Shared TUI apps use their normal port terminal interface
and ASCII glyphs. This does not yet expose the upstream pixel graphics API or
graphical app variants. Unsupported Unicode characters display as `?`; this is
not a complete xterm implementation.

Each console has its own shell context, directory, input parser and retained
parent/child app frames. Shared app registry claims prevent simultaneous use of
the same singleton app. Audio apps are mutually exclusive across both consoles;
the existing single worker reservation also limits simultaneous worker apps.
These are two trusted consoles on one OS, not separate security identities or
isolated filesystems. Saved identity and storage are shared. Command history is
per session in memory but uses the existing shared persistence file.

The local console starts without a USB CDC connection. The startup script runs
there once per boot. USB reconnect restarts only the USB session. USB dimensions
retain the saved settings; LCD geometry is fixed and rejects `setterm size`.
Keyboard arrows, Home/End, Page Up/Down, Insert/Delete, F1–F12 and Ctrl combinations
are translated through the shared VT100 input decoder.

Two console tasks use one recursive gate to serialize lifecycle and filesystem
operations. Python cancellation polls, synchronous audio polls, terminal reads
and `wait` release the gate at safe boundaries. This lets the other console run
without entering storage operations halfway through one. Other blocking native
operations can still delay the other prompt; this is not unrestricted parallel
execution of arbitrary applications. The second task has a 40 KiB internal
OCRAM stack. LCD SPI rendering is bounded and protected by the bus lock.

Diagnostics from USB:

```text
lcd
lcd dump
lcd send "echo hello from the local terminal"
lcd key ctrlc
lcd key exit
```

`send` types into whichever local app is active and appends Enter. It is intended
for testing while the local operator is idle. It is not a separate shell launch.
`dump` reads the text model, not the LCD's pixels. `lcd` also reports USB host
registers and keyboard detection to distinguish cabling from HID problems.

Build/upload: `pio run -e teensy41_display` (add `-t upload`). Close serial
monitors and run hardware suites one at a time. Baseline LCD-only firmware is
preserved alongside the repo in `solar_os-baselines/2026-09-27-lcd/`; the previous
apps/synth images remain there in their own directories.

## Validation in progress — 2026-09-27

- Host terminal tests passed cursor addressing, erase, attributes, footer scroll
  region, wrapping, unsupported UTF-8 and 200,000 random input bytes with UBSAN.
- Existing child lifecycle ASAN/UBSAN checks passed.
- All 14 manual-generator tests passed, and the previous `teensy41_synth`
  profile still builds. Final display image: 906,952 bytes flash, 440,224 bytes
  RAM1 and 225,680 bytes RAM2.
- `/tmp/teensy-display.json`: independent output and working directories,
  singleton app conflict, LCD Python loop with responsive USB, owner-specific
  cancellation, concurrent `wait`, ten calc restart cycles with stable memory,
  and USB reconnect preserving LCD text passed. Free heap 32,628 bytes; free
  PSRAM 8,366,736 bytes on the first build, before adding HID parsers.
- `/tmp/teensy-display-files.json`: existing Files regression passed SD/flash
  copy/move, recursive copy, ZIP, editor/Python child return and cleanup.
- Synth regression could not run: user removed the SGTL5000 shield to connect
  the USB host cable. `SGTL5000=missing` is expected in that hardware arrangement.
- User sees the local shell prompts. Microsoft keyboard 045e:0750 enumerates
  successfully after adding HID parsers. User confirmed physical typing and Files
  navigation both work on the LCD. Earlier automated local input used the diagnostic queue.

The initial USB host code lacked HID parser objects. Three parsers have now been
added. The user's initial home-built keyboard did not enumerate; they replaced
it with the Microsoft keyboard. Do not infer home-built keyboard compatibility
from the Microsoft result.

Reproduce host tests with `bash scripts/ports/test_teensy41_lcd_host.sh`.
Device suite: `python3 scripts/ports/test_teensy41_display.py --log /tmp/display.json`.
Use `--status-only` for a read-only keyboard/host status check. The full suite
requires the local operator to leave the keyboard idle and temporarily launches
apps on the LCD. Existing serial tests remain applicable one at a time.

Firmware/log/source snapshot: `../solar_os-baselines/2026-09-27-display/`.

## Plot and Playground extension

The current display profile also includes native RA8875 Plot graphics and the
Playground browser/download service. See [Plot/Playground](teensy41-plot-playground.md)
for commands, RTC setup, memory policy and validation. The original display
checkpoint above remains available as a rollback baseline.

### 2048 homebrew input correction

The public-domain [mmuszkow/2048-gb](https://github.com/mmuszkow/2048-gb)
ROM's `play()` function samples `joypad()` for the move and again in an
unconditional release-wait. A press arriving between those checks is swallowed.
The patch in `scripts/ports/fixtures/2048-input.patch` samples once and returns
on zero input, only waiting for release after processing a nonzero sample.
This is a ROM fix, not a change to emulated Game Boy timing or button semantics.

Rebuild from upstream commit `c7e25ee10e0e1abea043dc44be11ac32ea543553`, using
[GBDK 4.5.0](https://github.com/gbdk-2020/gbdk-2020/releases/tag/4.5.0):

```sh
# In the upstream 2048-gb checkout:
git apply /path/to/solar_os/scripts/ports/fixtures/2048-input.patch
/path/to/gbdk/bin/lcc -Wa-l -Wl-m -Wl-j -Wm-yc -Wm-yn2048 -o 2048-fixed.gb 2048.c
# In the SolarOS checkout, with original and corrected ROM files:
python3 tests/ports/test_teensy41_2048_rom.py --original /path/to/original/2048.gb --fixed /path/to/2048-fixed.gb
```

The lowercase `-Wm-yc` marks DMG/Color compatibility; uppercase `-Wm-yC`
marks Color-only and SolarOS correctly rejects it. Keep the upstream LICENSE.
The test boots 12 games and checks 96 legal moves at five taps per second:
original 67 accepted/29 missed, corrected 96 accepted/0 missed. An unmodified
source rebuild with the same compiler accepted 62/96, confirming the source
change fixes the missed input.

The SD card now contains the corrected `/sd/roms/2048.gb` and the preserved
`/sd/roms/2048-original.gb`. Both were verified by FTP readback.
Corrected ROM SHA-256: `a433fdc9befe3b2b5b8b6d862c4fae4ce9ef61cfc714be067150e70a78584ed1`.

`lcd small input` reports a bounded 32-event timing trace: `I` is the mask sent
by the app, `E` the mask sampled for emulation, and `D` a changed image completed
on the display. Times are milliseconds since boot; masks use Peanut-GB button
bits. Maximum input/frame gaps reset on game launch and include pauses.

The small dashboard Network page shows Ethernet and Wi-Fi connection states and
IP addresses, plus the Wi-Fi SSID (up to 21 characters) and RSSI in dBm. Wi-Fi
details come from the normal network poll, without extra bridge requests.

The `teensy41_power_board` profile adds `lcd small page power` with common-path
voltage/current/watts, three valid inputs, USB-PD settings, host status and saved
current-trip threshold. NES Left/Right cycles seven pages, including Level and
Compass. Missing power hardware
shows `unconnected`; see [power path](teensy41-power-path.md) for GPIO assignments
and the direct hardware overcurrent shutdown behavior.

## Digital level and compass

The `teensy41_power_board` profile enables `SK_MOTION=1` for the motherboard
`Environment.kicad_sch`: U17 LSM6DSOX (SA0 grounded, I2C address `0x6a`) and
U19 MMC5983MA (address `0x30`). Both use Wire1, SDA GPIO17 / SCL GPIO16, with
CS tied high for I2C. The schematic routes IMU INT1 to GPIO22 and magnetic INT
to GPIO38; this driver polls data-ready registers and does not configure those
GPIOs. BME280 is independent and is not changed.

Open `lcd small page level` or `lcd small page compass`, or use NES Left/Right.
Select returns home. Both onboard and external controllers support the controls.
Sensor pages refresh at 10 Hz; other dashboard pages retain their 1 Hz refresh.
They yield the display to apps and the console using the existing ownership rules.

Level shows roll/pitch in degrees and a centered bubble (green within 0.5 degrees
on both axes). A sets the current angle as a relative zero; B restores absolute
level. Keep still: readings outside 0.85–1.15 g are rejected. Roll/pitch use the
LSM6DSOX package X/Y/Z frame, not a case mounting calibration.

Compass shows **magnetic**, not true, north, with an eight-point direction and
north pointer. Hold the PCB flat and still (about 15 degrees maximum tilt).
The reference is the MMC5983MA package +X direction; heading increases clockwise
with +Y treated as right. The PCB places U17 at 0 degrees and U19 at 90 degrees;
the compass deliberately uses a flat measurement rather than assuming their axes
align for tilt compensation. Confirm the forward edge and heading sense on the
assembled board before relying on case markings.

On Compass, press A, slowly rotate the complete device through all orientations,
then A again to save. At least 50 fresh samples and 20 microtesla span on every
axis are required; inadequate coverage keeps calibration running. B cancels a
new calibration and keeps any previous solution. Calibration applies hard-iron
offset and diagonal scale correction, lasts for this boot, and does not correct
arbitrary cross-axis distortion. The page labels uncalibrated readings. Keep away
from magnets, speakers, and high-current wiring; implausible field readings are
suppressed. No declination correction is applied.

The transport uses the existing I2C mutex per transaction and never holds SPI
while reading sensors. LSM6DSOX uses 52 Hz accelerometer/gyro outputs, +/-2 g
and +/-250 dps ranges, block-data update and auto-increment. MMC5983MA uses
18-bit output, automatic set/reset and continuous 20 Hz measurements. Each
device checks its identity, recovers independently at two-second intervals, and
expires data after 500 ms. Missing or stale hardware displays a status message
and clears the bubble/pointer. Sensor initialization is lazy, on opening a sensor
page; builds without `SK_MOTION` show that sensors are disabled.

Register references: [ST LSM6DSOX datasheet](https://www.st.com/resource/en/datasheet/lsm6dsox.pdf)
and [MEMSIC MMC5983MA datasheet](https://www.memsic.com/Public/Uploads/uploadfile/files/20220119/MMC5983MADatasheetRevA.pdf).
Run `bash scripts/ports/test_teensy41_motion_host.sh` for register, decoding,
failure/recovery, angle, calibration and display-control/bounds tests.
Hardware heading orientation, noise and accuracy remain bench acceptance items.

## Two physical controller inputs — 2026-10-26

The `teensy41_power_board` profile now reads **on-board data on GPIO4** and
**external NES data on GPIO6**, sharing **clock2 / latch3**. GPIO6 is U1's KiCad
symbol/package pin8; Teensy GPIO8 remains the SGTL5000 audio input. The saved
schematic and user confirmation establish this distinction. No jumper selection
is needed. Older profiles keep their original single data input and USB fault
pin6 allocation; `SK_NES_CONTROLLER_DATA_PIN` defaults to disabled outside the
power-board profile. Resource reservations reject GPIO reuse and compile checks
reject overlap with codec pins or the legacy USB control pins.

Each source has its own 20ms debounce and held mask. The dashboard, Music page,
DMM and current single-player Game Boy input use their bitwise union: either
source works, simultaneous presses combine, and releasing one source cannot
release a button still held by the other. Owned displays suppress dashboard
navigation while the physical states continue updating. An unplugged data line
is pulled high/idle. The existing grounded-line filter applies independently,
so an invalid stream cannot suppress the healthy source.

Future two-player applications can include `small_display.h` and use:

- `sk_small_controller_count()` — supported physical input slots (not presence
  detection); two in the power-board profile, one in older single-input builds.
- `sk_small_controller_snapshot(uint8_t buttons[2])` — one atomic pair snapshot;
  index0 is the on-board buttons and index1 the external NES controller.
- `sk_small_controller_buttons(index)` — one source; unsupported indices return0.
- `sk_small_buttons()` — merged physical mask retained for existing apps.

Masks use A=0x01, B=0x02, Select=0x04, Start=0x08, Right=0x10, Left=0x20,
Up=0x40 and Down=0x80. These APIs never include or consume keyboard events.
Game Boy still adds its keyboard mapping separately. This makes the two physical
inputs available independently; it does not add multiplayer to the Game Boy
emulator or an existing single-player game.

### Capture timing and diagnostics

Both inputs are captured in the same 100Hz scan. GPIO2/3/4 use FlexIO1; GPIO6 uses
FlexIO2. The primary block generates the shared physical latch/clock waveform.
The secondary block has an input shifter, DMA buffer and internal sampling timer
with **exactly the same slot period**. An internal dummy TX triggers that timer
without driving another pin. Adjacent peripheral writes launch both timers with
IRQs masked only for those writes. No per-edge CPU interrupts or busy waits are
added to the normal path. A pair is published only when both captures complete;
a failed/timed-out side makes the reader fall back to GPIO for both, still using
one common latch and eight clock pulses. Busy blocks are not retuned, and another
driver's shifters/timers are not released. Each DMA buffer has its own cache line.

`lcd small` reports `nes=flexio-dual-dma`, scan/fault counters, `pads=2`, and
separate hex `onboard`/`external` masks. This lets physical testing distinguish the
sources without a game or synthetic input. Check idle=00, then each source
separately and simultaneously; unsupported/unplugged input is not a second
player presence signal. `lcd small input` remains the existing Game Boy timing
trace of the final app input.

Host checks: `python3 tests/ports/test_teensy41_nes_flexio.py` covers both DMA
blocks, all serial bit patterns, matched clock/resource failures, either-side
errors/timeouts, fallback and IRQ preservation. The navigation test covers all
65,536 input pairs and independent debounce/holds. `python3
tests/ports/test_teensy41_dual_input.py` exercises the production publication,
navigation/Music, Game Boy/keyboard merge, ownership and shutdown APIs. Real
buttons and cross-block sampling phase still require the physical tests in the
[master checklist](teensy41-test-checklist.md#two-physical-controller-inputs--2026-10-26).

The updated power-board image was built/flashed and read back over USB: both DMA
captures completed continuously (`flexio-dual-dma`, `pads=2`), with zero reader
faults across six observations/576 paired scans. Idle masks were00/00; physical
presses, shared-clock timing and concurrent-load validation remain pending.
