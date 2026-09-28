# Temporary RA8875 LCD bring-up

Work started 2026-09-27. The user is testing a roughly 4-inch RA8875 display
before fitting a 7-inch display later. This wiring differs from the PCB.

| LCD signal | Teensy pin |
| --- | --- |
| MOSI | 11 |
| MISO | 12 |
| SCK | 13 |
| CS | 37 |
| RESET | 9 |

The user's initial resolution estimate was 400x800. The Adafruit 800x480
preset has now produced readable text on hardware. Ground is common with
the Teensy; wiring changes must be made with all power disconnected.

User subsequently supplied these markings: `10050020 131015`,
`KD50G21-40TT-A2`, `KW201312048`, `20131223-1`. No exact datasheet match
was found for the TT-A2 suffix. The related KD50G21-40NT-A1 manufacturer's
[datasheet](https://www.electrokit.com/upload/product/41013/41013513/KD50G21-40NT-A1.pdf)
identifies a 5-inch 800x480 panel. This makes 800x480 a likely starting point,
not a confirmed specification for this exact variant. These panel markings
do not identify the RA8875 adapter board. User has now confirmed that the
controller board and display were bought from Adafruit. They are now connected.
The test profile selects `Adafruit_800x480`, enabling the adapter's
GPIOX display signal. Wire with all power disconnected before hardware testing.

`teensy41_lcd` is a separate bootstrap console profile, not the combined
synth/apps firmware. It initializes the display and prints a startup message,
then mirrors bootstrap console output. Full ANSI terminal, graphics service,
touch and upstream app integration are not implemented here.

Build with `pio run -e teensy41_lcd`. Its PlatformIO flags select
`SK_PRIMARY_CS`, `SK_PRIMARY_RESET`, `SK_PRIMARY_PANEL`, and
`SK_PRIMARY_ROTATION`. PCB defaults remain CS 9/reset 14 in `board.h`.
SPI0 uses 4 MHz writes and 2 MHz reads for initial bring-up.

CS 37 overlaps expansion slot 0 in the PCB map. Display initialization reserves
that slot as well as slot 2, preventing expansion commands from selecting the
LCD. Reset 9 also overlaps the original slot 2 select; that slot is reserved.
Do not attach expansion devices to these reused signals during this test.

The previous synth baseline is preserved in
`../solar_os-baselines/2026-09-27-synth/`. The older `teensy41_peripheral_check` build currently fails
on a missing `Audio.h` include; use the dedicated LCD profile for this work.

The dedicated profile compiled successfully with CS 37/reset 9 on 2026-09-27:
RAM1 165,920 bytes, RAM2 14,296 bytes, flash 183,588 bytes. Build log:
`/tmp/solar-teensy-lcd-build.log`. Compilation does not validate the panel.

## First hardware test — 2026-09-27

Uploaded the Adafruit LCD profile successfully; the loader retried a USB write
before completing. Log: `/tmp/solar-teensy-lcd-upload.log`.
User confirmed that text is visible and readable on the display.

`test_teensy41_serial.py --expect mounted --repeat 5` passed console,
calculator and SD checks with the LCD enabled. SD mounted on its first attempt
in 9 ms; free heap was stable at 327,928 bytes and console stack headroom was
3,274 words. Log: `/tmp/teensy-lcd-console.json`. Test process exited normally.

This validates initial text output, not full-screen geometry, color accuracy,
touch, or long-term stability. Library `errorCode()` only reports configuration
errors; it is not a controller identification/readback test.

Installed firmware is now the LCD bootstrap profile, not synth/apps.
Firmware, logs and source snapshot: `../solar_os-baselines/2026-09-27-lcd/`.
