# SolarOS on Teensy 4.1 / SuperKeyboard

This experimental port runs the shared SolarOS shell and text applications on
a Teensy 4.1. The current `teensy41_display` profile has an RA8875 LCD with a
USB host keyboard and a separate USB serial console. Both can be used at once.
Touch is not required or enabled.

## Tested hardware and wiring

- Teensy 4.1 with 8 MiB PSRAM, fitted 16 MiB QSPI flash, and an SD card in the
  Teensy's native SDIO socket. The combined apps use PSRAM; saved preferences
  require the flash filesystem to be mounted.
- Adafruit RA8875 driver and KD50G21-40TT-A2 LCD, working with the
  `Adafruit_800x480` preset. Related panel documentation identifies this family
  as 5-inch 800x480; an exact datasheet for this suffix was not found.
- Microsoft USB keyboard `045e:0750` on the Teensy's separate USB host connector.
  Typing and Files navigation are hardware-verified. The initial home-built
  keyboard did not enumerate; its compatibility remains unresolved.
- Optional PJRC Ethernet kit and SGTL5000 Rev D audio shield. The shield was
  removed to access the USB host connector during display testing; audio was
  tested separately with the synth profile.

Temporary display wiring differs from the SuperKeyboard PCB:

| Adafruit RA8875 signal | Teensy connection |
| --- | --- |
| MOSI | 11 |
| MISO | 12 |
| SCK | 13 |
| CS | 37 |
| RST | 9 |
| VIN | USB 5V / VUSB |
| GND | GND |

Connect or change wiring with all power disconnected. Leave 3Vo, WAIT, INT,
LITE and touch pins unconnected for this configuration. Older Adafruit RA8875
boards do not release MISO; avoid sharing SPI0 with another active SPI device.
The Teensy's built-in SD socket uses a separate interface. See the
[Adafruit board guide](https://learn.adafruit.com/ra8875-touch-display-driver-board?view=all).

CS 37 and reset 9 overlap expansion-slot signals, which the firmware reserves
for the display. Do not attach expansion devices to those reused signals.
Change `SK_PRIMARY_CS`, `SK_PRIMARY_RESET`, `SK_PRIMARY_PANEL`, and
`SK_PRIMARY_ROTATION` in the selected PlatformIO profile when moving to another
panel or the PCB. The terminal renderer currently assumes landscape 800x480;
other dimensions/rotations also require terminal geometry changes.
Consult the [PCB hardware findings](teensy41.md#unresolved-wiring-and-population)
before using the custom board, particularly its audio supply wiring.

## Build and upload

Run from the repository root with PlatformIO installed. The first build fetches
the pinned Teensy platform and libraries.

```sh
pio run -e teensy41_display
pio run -e teensy41_display -t upload
pio device monitor -e teensy41_display --baud 115200 --raw --exit-char 28
```

Close serial monitors and hardware-test scripts before uploading. Ctrl+\ exits
the monitor; Ctrl-C then remains available to the SolarOS application. If the
automatic reboot cannot reach the Teensy, press the board's Program button when
the loader waits for the device. Firmware is in `.pio/build/teensy41_display/`.

Always select an environment explicitly: the repository default is an ESP32
target. Useful profiles are:

| Profile | Purpose |
| --- | --- |
| `teensy41_display` | Combined apps/synth plus independent LCD and USB terminals |
| `teensy41_synth` | Combined apps plus terminal synth, USB console only |
| `teensy41_apps` | SSH, Files, persistent settings, less, Notes and Sheet |
| `teensy41_lcd` | Minimal LCD/bootstrap console for hardware diagnosis |
| `teensy41` | Original bootstrap recovery console |

Earlier shell/audio/network/SSH/Files profiles remain available for focused
bring-up. `teensy41_peripheral_check` is an older compile-only experiment with
unconfirmed secondary-display wiring and a known `Audio.h` dependency issue;
use the tested profiles above.

## Everyday use

The LCD starts its shell at boot. The local USB host keyboard controls it. A
computer attached to the Teensy's USB device port gets a separate shell prompt.
Each console retains its own directory, input and foreground app. USB reconnect
does not clear the LCD session. These are trusted consoles sharing one OS,
filesystem, identity, clipboard and peripherals, not isolated user accounts.

```text
help
files /
calc
edit /flash/example.txt
less man:app.notes
notes /flash/tasks.md
sheet /sd/readings.csv
python
mem
```

Files uses arrows and function keys; Q exits. Ctrl-] is the general app exit key.
Notes edits Markdown checklists; Sheet views CSV data/formulas rather than
editing cells. `synth` plays through the audio shield when fitted; see its
[controls](teensy41-synth.md). It starts at 20% headphone volume.

`/` lists the `/sd` and `/flash` mounts. Identity, USB terminal geometry and
startup selection persist in flash. `setterm size 100 30` changes USB geometry;
LCD geometry is fixed at 100x30. In the display profile the startup script runs
once per boot on the LCD session. No startup file is created automatically.

Ethernet must be started after reboot before using SSH or networked Python:

```text
network up
network status
network routes
```

Wait for `link=up`, a nonzero address and `DHCP=bound`. To restart Ethernet/DHCP,
run `network down` then `network up`; this drops existing connections. There is
no dedicated release/renew command or continuous network status display yet.

From USB, `lcd` reports terminal/keyboard status and `lcd dump` reads the text
buffer. `lcd send "COMMAND"` and `lcd key exit|ctrlc|esc` inject local input for
diagnostics; use them only while the local operator is idle.

## Validation and current limits

Hardware checks passed independent output/directories, app ownership, Python
cancellation while the other console remains responsive, repeated app cleanup,
USB reconnect, and Files storage/editor operations. Physical keyboard typing
and Files navigation on the LCD were confirmed. Host terminal and synth-engine
tests, app lifecycle sanitizers, and all 14 manual-generator tests passed.
See [display evidence](teensy41-display.md) and [synth evidence](teensy41-synth.md)
for the exact scope and recorded logs.

```sh
bash scripts/ports/test_teensy41_lcd_host.sh
bash scripts/ports/test_teensy41_synth_host.sh
bash scripts/ports/test_teensy41_children_host.sh
python3 -m unittest discover -s tests -p test_generate_manual.py
python3 scripts/ports/test_teensy41_display.py --log /tmp/display.json
```

Hardware tests require pyserial, exclusive USB access and an idle local keyboard.
The Files/apps suites additionally use pyte; SSH fixtures use Paramiko. Run
hardware suites one at a time. Tests that write storage retain uniquely named
fixtures; read each script's instructions before running it. See the
[hardware test guide](../../scripts/ports/README.md).

- Shared terminal/TUI apps work; the pixel graphics API and graphical app
  variants are not yet integrated. The local terminal uses ASCII; unsupported
  Unicode becomes `?`. It is not a complete xterm emulator.
- Singleton apps cannot run twice. Audio apps share one output, and worker apps
  share the existing worker reservation. Native operations without a cooperative
  polling point can delay the other prompt.
- Audio and display/keyboard have been tested separately. Combined audio/display
  hardware testing awaits reconnecting the shield.
- SD hot-removal recovery, broad USB keyboard compatibility, full-capacity PSRAM
  testing, secondary display and more upstream services remain pending.
- A previous loader rejected an image beyond the 1 MiB Intel HEX boundary.
  The current display image is about 907 KB; investigate the uploader before
  exceeding that boundary. It is not the board's flash-capacity limit.

## Further notes

- [Current handoff](teensy41-handoff.md)
- [Progress tracker and backlog](teensy41-roadmap.md)
- [Detailed port history and hardware findings](teensy41.md)
- [Initial LCD bring-up](teensy41-lcd.md)
- [Display/session implementation](teensy41-display.md)
- [Synth implementation and controls](teensy41-synth.md)

Local tested firmware/log/source snapshots are stored alongside this checkout
in `../solar_os-baselines/`. They are recovery artifacts, not repository files.
Unfinished DNP3 sources in the development workspace are not integrated or part
of the tested port.
