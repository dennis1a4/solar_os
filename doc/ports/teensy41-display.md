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
