# Teensy 4.1 handoff — 2026-09-27

Start with the [Teensy README](README.md). Repository:
`/home/dennis/Documents/SuperKeyboard/Code/solar_os`, branch `main`.
The user requested cleanup and a local commit of the completed synth/display
work. No push was requested. Preserve unfinished, untracked DNP3 sources;
they are not included in the tested profiles or this checkpoint.

## Installed and confirmed

Profile `teensy41_display`: independent LCD/USB-host-keyboard and USB CDC
SolarOS shells, extending the existing apps/synth profile. Temporary Adafruit
RA8875 wiring: MOSI 11, MISO 12, SCK 13, CS 37, RESET 9; preset
`Adafruit_800x480`, 100x30 text terminal. User confirmed readable text, typing,
Files navigation and overall operation. Microsoft keyboard 045e:0750 works;
the initial home-built keyboard did not enumerate. Touch is out of scope.

The user removed the SGTL5000 shield to access the USB host connection.
`SGTL5000=missing` is expected; audio hardware regression cannot run until
it is reconnected. The synth was separately built, uploaded and tested before
that removal. No audible headphone listening check was recorded.

Display firmware: `.pio/build/teensy41_display/firmware.hex`.
SHA256: `9156e51ca7cb3d1b4bbef390c185495807a6f3e10f5e640e62c61732f0281667`.
Flash 906,952 bytes, RAM1 440,224 bytes, RAM2 225,680 bytes.
Permanent baseline: `../solar_os-baselines/2026-09-27-display/`.
Prior LCD-only, synth, apps, Files and SSH backups remain in that directory tree.

## Validation

- `/tmp/teensy-display.json`: independent output/directories, singleton app
  conflict, local Python loop with responsive USB, owner-specific cancellation,
  concurrent wait, ten calc restart cycles with stable heap/PSRAM, and USB
  reconnect preserving LCD state.
- `/tmp/teensy-display-files.json`: Files SD/flash copy/move, recursive copy,
  ZIP, editor/Python child return and cleanup.
- `/tmp/teensy-display-keyboard.json`: Microsoft keyboard enumeration; physical
  typing and Files navigation subsequently confirmed by the user.
- Host ANSI terminal UBSAN, synth-engine ASAN/UBSAN, child lifecycle ASAN/UBSAN
  and 14 manual-generator tests pass. The previous USB-only synth profile builds.
- Automated two-session/Files checks preceded the final HID parser additions;
  the final firmware passed enumeration and physical keyboard/display checks.

The [display notes](teensy41-display.md) and [synth notes](teensy41-synth.md)
record architecture, reproduction and limitations. Serial test processes have
exited. Do not inject LCD commands while the local operator is typing.

## Continuing work

The display implementation covers shared terminal/TUI apps, not pixel graphics
or graphical app variants. Singleton apps and the single worker/audio resource
remain shared; native blocking operations without cooperative polling can delay
both consoles. Startup runs on the local LCD session once per boot; USB geometry
remains persistent while LCD geometry is fixed. Ethernet requires `network up`
after reboot; `network down` / `network up` restarts DHCP.

Next work should follow the user's chosen priority. Possible follow-ups are
combined display/audio validation after refitting the shield, home-built
keyboard compatibility, broader ANSI/Unicode or pixel graphics, and the
[roadmap](teensy41-roadmap.md). Do not automatically resume DNP3. Its preserved
scope was a two-way 3.3 V UART bridge on Serial7/Serial8, configurable from
9600 baud, then a TCP proxy/viewer; its sources remain unbuilt and untested.

Use separate build directories when needed and keep known-good firmware before
hardware changes. Serial tests must run one at a time. Host USB devices may be
hidden by the sandbox; use approved host access before reporting disconnection.
The loader sometimes retries a USB write and succeeds. A prior image above
1 MiB hit a loader/HEX boundary issue; the board's flash capacity is larger.
