# Paused graphics checkpoint — 2026-09-27

The user explicitly requested an overnight pause. Resume graphics/View work
when asked. See [graphics notes](teensy41-graphics.md). Current branch is
`teensy41`; preserve the unrelated untracked DNP3 work.

## Current work in progress

- Uploaded display firmware adds the shared PSRAM INDEX8/u8g2 renderer and
  fonts/icons, native View/Invaders, and synchronous Python `solaros.gfx`.
- User visually confirmed seeing the color test patterns. PNG/JPEG hardware
  viewing and fit/actual transitions ran. Python drawing and exception cleanup
  checks were reached; Invaders was animating when the user requested pause.
- **The full hardware test was intentionally interrupted, not passed.**
  Log: `/tmp/teensy-graphics.json`; last completed frame count 15. Do not report
  full graphics validation complete. The interrupted test is saved with the
  checkpoint. Host PNG/JPEG ASAN/UBSAN tests and 14 manual tests passed.
- Full-screen presentation is currently **about 1.73 seconds**. This is too slow
  for smooth games. Next work should optimize presentation before calling the
  graphical-app port complete. The shared canvas already supplies dirty tiles
  and 6,000 presented-hash slots: compare tile content (and palette changes) so
  apps which clear/redraw a frame do not force retransmission of unchanged
  tiles. No hash optimization has been implemented yet. Validate bounds and
  palette/rotation/dirty-map assumptions in the new surface adapter as well.
- Current SD demos: `/sd/graphics-demo-acaf29fc/color-bars.png`, `color-bars.jpg`,
  `drawing.py`, `broken.png`. Earlier interrupted test folders
  `/sd/graphics-demo-fc0cc8cd` and others may remain. Do not delete user files.
- The old uploader failed at the 1 MiB HEX boundary. Pinned PJRC CLI 2.3 revision
  `03fca4156c244c7ad36bd368cf6e24531dbd566a` fixed this. Normal Linux PlatformIO
  upload now calls `scripts/ports/upload_teensy41.py`, builds the tool in
  `.pio/teensy-tools/loader`, and explicitly selects `TEENSY41`. This path was
  successfully tested. Host prerequisites: make, C compiler, libusb-compat.
- Current firmware SHA256:
  `bb46b976d2b55f5d8a5e3a014d263d740c3f7a221264938f2c745f24476bd08d`.
  Flash 1,202,984 bytes. Backup firmware, ELF, source and logs:
  `../solar_os-baselines/2026-09-27-graphics-wip/`.
- View/gfx diagnostic logs are suppressed by default to preserve USB prompt
  isolation (`SK_GFX_DIAGNOSTICS=1` opts in). WebP and Lua are unavailable.
  Python graphics bindings exist; other upstream Python modules are not all
  ported. Native Invaders has no audio here (shield remains disconnected).
- The hardware test transfers images through Python REPL; shell command lines
  are too short for large hex payloads. It polls frame counts because display
  presentation yields to USB before the frame finishes. Remaining: finish
  native/Python controls and memory tests, exercise official Playground
  Mandelbrot, Files-to-View child return, Plot and dual-console regressions,
  and obtain visual confirmation of text/icons/game controls.
- Official Mandelbrot package inspected at `/tmp/solaros-mandelbrot.sopkg`;
  SHA256 matches catalog `43fc83c5448955b418641221ee3b748ceccd8f062add9ba52159cf3d60f9b6f0`.
  Uses only the supplied graphics/should_exit/getch API. Not yet run on device.

The sections below preserve the previous completed Plot/Playground baseline;
they do not describe the newly uploaded graphics checkpoint.

---

# Teensy 4.1 handoff — 2026-09-27

Start with the [Teensy README](README.md). Repository:
`/home/dennis/Documents/SuperKeyboard/Code/solar_os`, branch `teensy41`, tracking `origin/teensy41`.
The public fork is https://github.com/dennis1a4/solar_os; `upstream` points to
https://github.com/nilseuropa/solar_os. The previous synth/display checkpoint
was pushed as `f28a885` before the Plot/Playground work. Preserve unfinished, untracked DNP3 sources;
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

Previous display-only integration baseline (before Plot/Playground):
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

## Plot/Playground checkpoint

The current `teensy41_display` build adds Plot's RA8875 drawing backend, an
`uptime` stream, Playground with verified HTTPS and persistent catalog settings,
and `rtc` / `rtc set <UTC Unix seconds>`. The user plans a coin-cell RTC backup;
battery retention has not been physically tested. Store UTC in the RTC.
PSRAM remains the preferred large application-data allocator; internal stacks
and timing-critical state stay internal. Shared external BSS is explicitly
zeroed before service initialization.

Hardware checks exercised live/CSV plotting with responsive USB, repeated Plot
cleanup, catalog refresh, Hello Python installation/run/alias, and rejection of
unsupported Lua/Python graphics entries. TLS initially correctly rejected an
incorrect RTC date. Enabling mbedTLS assembly and NIST arithmetic optimizations
reduced certificate processing enough for the catalog GET to complete in about
four seconds. Diagnostic output is disabled by default (`SK_HTTP_DIAGNOSTICS=1`
opts in). The official catalog and Hello Python are retained for manual use.

Current firmware: `.pio/build/teensy41_display/firmware.hex`, SHA256
`bfba482edb0564644527967a4ed5fde7affaed799c61ca3ebcb90309defea755`.
Flash 1,015,088 bytes; RAM1 424,256 bytes; RAM2 225,704 bytes.
Firmware and validation logs: `../solar_os-baselines/2026-09-27-plot-playground/`.
Final combined app test: `/tmp/teensy-plot-playground-final.json`;
independent-console regression: `/tmp/teensy-plot-display-regression.json`.
Both pass. HTTP/settings/child lifecycle/terminal host tests pass, as do the
14 manual tests and the USB-only synth build. The user confirmed a readable live Plot graph and working Space pause/resume
and Q-to-shell keyboard controls.

## Continuing work

The display now also supports native Plot graphics and the Playground catalog
browser; see [Plot/Playground notes](teensy41-plot-playground.md). Lua and Python
graphics bindings remain unavailable. Other graphical apps still need porting. Singleton apps and the single worker/audio resource
remain shared; native blocking operations without cooperative polling can delay
both consoles. Startup runs on the local LCD session once per boot; USB geometry
remains persistent while LCD geometry is fixed. Ethernet requires `network up`
after reboot; `network down` / `network up` restarts DHCP.

Next work should follow the user's chosen priority. Possible follow-ups are
combined display/audio validation after refitting the shield, home-built
keyboard compatibility, broader ANSI/Unicode or additional graphics primitives, and the
[roadmap](teensy41-roadmap.md). Do not automatically resume DNP3. Its preserved
scope was a two-way 3.3 V UART bridge on Serial7/Serial8, configurable from
9600 baud, then a TCP proxy/viewer; its sources remain unbuilt and untested.

Use separate build directories when needed and keep known-good firmware before
hardware changes. Serial tests must run one at a time. Host USB devices may be
hidden by the sandbox; use approved host access before reporting disconnection.
The loader sometimes retries a USB write and succeeds. A prior image above
1 MiB hit a loader/HEX boundary issue; the board's flash capacity is larger.

Final Files regression passed: SD/flash copy/move, recursive copy, ZIP, editor
and Python child return, and repeated app cleanup. The memory assertion accepts
increased internal free space while still rejecting losses; PSRAM must match.
Log: `/tmp/teensy-plot-files-regression-final.json`.
